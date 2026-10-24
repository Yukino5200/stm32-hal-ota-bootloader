/*
 * ============================ ESP32 → STM32 OTA 桥（WiFi 版） ============================
 *
 * 【这是个什么东西】
 *   ESP32 在这里扮演一个"无线转串口的中转站"：
 *   手机浏览器 --WiFi--> ESP32 --串口 921600--> STM32 的 BootLoader。
 *   于是原本必须"电脑 + SecureCRT + 手点菜单"才能做的升级，
 *   现在变成"手机连热点、点两下按钮"。
 *
 * 【整个系统的角色分工】
 *   · 手机浏览器  = 遥控器（发命令、选固件文件）
 *   · ESP32       = 翻译官（把 HTTP 请求翻译成串口字节流）
 *   · STM32       = 真正干活的人（擦 Flash、收固件、跳新程序）
 *
 * 【和 STM32 侧 boot.c 的"暗号约定"（两边必须一模一样）】
 *   · 上电后 5 秒内收到小写 'w' 就进命令行
 *     —— 注意 STM32 只认接收缓冲区第 0 个字节，所以这里每次都只发 'w' 这一个字节
 *   · 命令行菜单是"单字符命令"：1 擦A区 / 2 串口IAP / 3 4 版本号 / 7 重启 / 8 清标志
 *   · 按 '2' 之后，STM32 每秒发一个 'C'，表示"我准备好了，请用 Xmodem 协议发固件"
 *   · Xmodem 一帧固定 133 字节：0x01 + 包号 + (~包号) + 128字节数据 + CRC16(高字节,低字节)
 *   · CRC16 参数：多项式 0x1021，初值 0x0000（和 boot.c 里的 Xmodem_CRC16 完全一致）
 *   · 回复暗号：0x06=ACK(收到，继续)  0x15=NAK(有问题，重发)  0x04=EOT(发完了)
 *
 * 【接线表】
 *   ESP32 GPIO17 (TX2) ──> STM32 PA10 (USART1_RX)      "我发你说"
 *   ESP32 GPIO16 (RX2) <── STM32 PA9  (USART1_TX)      "你说我听"
 *   ESP32 GND          ─── STM32 GND                    "必须共地，否则电压没有参考"
 *   ESP32 GPIO4        ──> STM32 NRST                   "按复位键的手"（不接也能用，但要手按）
 *
 * 【一个必须注意的坑】
 *   调试用的 CH340 和 ESP32 都会往 STM32 的 PA10 上发数据。
 *   两个"嘴巴"同时对着一个"耳朵"说话 → 数据会打架。
 *   所以测无线升级时，必须把 CH340 的 TX/RX 拔掉。
 * ======================================================================================
 */

#include <WiFi.h>       // ESP32 的 WiFi 库（板子包自带，不用装）——负责开热点
#include <WebServer.h>  // 极简 HTTP 服务器库——负责把网页发给手机、接住手机发来的请求

// ============================ 可改配置区（改这里就能调行为） ============================
const char *AP_SSID = "OTA-Bridge";      // 热点的名字（手机 WiFi 列表里看到的就是它）——像店门口的招牌
const char *AP_PASS = "Yukino5200";    // 热点密码，WPA2 要求至少 8 位
const uint32_t UART_BAUD = 921600;     // 和 STM32 说话的语速，必须与 usart.c 里的波特率一模一样
                                       //   两边不一致 → 收到的全是乱码（就像用 2 倍速听录音）
const int PIN_UART_RX = 16;            // ESP32 的"耳朵"接在 GPIO16（对应板子丝印 D16）← STM32 的 PA9
const int PIN_UART_TX = 17;            // ESP32 的"嘴巴"接在 GPIO17（对应板子丝印 D17）→ STM32 的 PA10
const int PIN_MCU_RST = 4;             // ESP32 用来"按 STM32 复位键"的引脚（D4）→ STM32 的 NRST
const bool USE_RESET = true;           // true = 用上面这根线自动复位；没接线就改成 false，改成手工按复位
const size_t MAX_BIN = 48 * 1024;      // 允许上传的最大固件：48KB（A 区只有 44KB，留点余量当保险）
// =======================================================================================

WebServer server(80);          // 开一个"前台"，监听 80 端口（浏览器访问 http://192.168.4.1 时默认就是 80 端口）
HardwareSerial McuSerial(2);   // 用 ESP32 的 2 号硬件串口（UART2）专门跟 STM32 说话——相当于一个对讲机

// ---- 下面这几个全局变量，是"后台干活的工人"和"网页"之间的共享黑板 ----
uint8_t *g_buf = nullptr;      // 上传上来的固件放在这块内存里（用 malloc 临时申请的"货架"）
size_t g_len = 0;              // 货架上实际放了多少字节
volatile bool g_busy = false;  // "正在忙"的牌子：true 时网页就知道别重复点按钮了
                               //   volatile 是告诉编译器：这个变量会被别的地方（另一个任务）改，别缓存它
volatile int g_pct = 0;        // 升级进度 0~100，网页每 400ms 来问一次，拿它画进度条
String g_msg = "空闲";         // 状态牌上写的话："空闲" / "升级中" / "升级成功" / "升级失败"
String g_log = "";             // 流水账本（最近若干条日志），方便以后想看时查

// ============================ 一、几个小工具函数 ============================

// 往"流水账本"里记一笔，同时抄一份到调试串口（方便你在电脑上看 ESP32 自己的输出）
void appendLog(const String &s) {
  Serial.println(s);              // 抄一份到 USB 调试串口（电脑上用串口工具能看到）
  g_log += s + "\n";              // 记进内存里的账本
  if (g_log.length() > 4000) {    // 账本太长了会吃内存，所以设个上限
    g_log = g_log.substring(g_log.length() - 3000);  // 超过 4000 字就只保留最后 3000 字（像剪掉旧账）
  }
}

// 算一段数据的 CRC16 校验值（就是给数据算个"指纹"）
// 算法和 STM32 侧 boot.c 的 Xmodem_CRC16 一字不差，否则两边算出来对不上，每帧都会被拒收
uint16_t crc16_xmodem(const uint8_t *data, size_t len) {
  uint16_t crc = 0x0000;              // 初值 0（Xmodem 规定的）
  for (size_t i = 0; i < len; i++) {  // 一个字节一个字节地"揉"进去
    crc ^= (uint16_t)data[i] << 8;    // 先把当前字节放到高 8 位，跟 crc 做异或
    for (int b = 0; b < 8; b++) {     // 再逐位处理 8 次
      // 最高位是 1 就左移后再异或多项式 0x1021；最高位是 0 就只左移
      crc = (crc & 0x8000) ? (uint16_t)((crc << 1) ^ 0x1021) : (uint16_t)(crc << 1);
    }
  }
  return crc;                         // 最后的 crc 就是这 128 字节数据的"指纹"
}

// 给 STM32 按一下复位键（把 NRST 拉低 120ms 再放开，等于你手指按一下松开）
void mcuReset() {
  if (!USE_RESET) return;                    // 没接线就不做，免得白等
  /* 开漏输出：只会"往下拉"，不会"往上推"。
     高电平时是"高阻态"（相当于松手），STM32 的 NRST 内部上拉自然把它拉高；
     只有拉低时才真正复位。这样平时完全不会干扰 STM32（不用外接上拉电阻）。 */
  pinMode(PIN_MCU_RST, OUTPUT_OPEN_DRAIN);
  digitalWrite(PIN_MCU_RST, LOW);            // 拉低 = 按住复位键
  delay(120);                                // 按住 120 毫秒（够 STM32 完成复位）
  digitalWrite(PIN_MCU_RST, HIGH);           // 放开 = 松开复位键，STM32 开始启动
  delay(120);                                // 等它启动稳定一下
}

// 把串口里"上一次剩下的"字节全部读走扔掉（像把杯子里的剩水倒掉，免得和新水混在一起）
void uartDrain() {
  while (McuSerial.available()) {  // available() = 现在有多少字节等着读
    McuSerial.read();              // 读一个扔掉（不保存）
  }
}

// 守在串口边听 ms 毫秒，听到什么都记下来，最后返回这串文字
// 用途：发完命令后，把 STM32 的回话收齐（比如菜单、版本号）
String uartReadFor(uint32_t ms) {
  String s;                          // 准备一个空口袋
  uint32_t end = millis() + ms;      // 算好"截止时间"：现在 + 要听多久（像微波炉定时）
  while (millis() < end) {           // 还没到点就继续听
    while (McuSerial.available()) {  // 有字节就赶紧收
      s += (char)McuSerial.read();   // 收进袋子
    }
    delay(2);                        // 歇 2ms 再听（别把 CPU 占死，也给别人机会干活）
  }
  return s;                          // 到点了，把口袋交出去
}

// 一边听一边找：只要收到的文字里出现 needle 这个词，立刻返回 true（不用等到超时）
// 举例：waitForText("命令行", 160) = "160 毫秒内，只要听到'命令行'三个字就算成功"
bool waitForText(const String &needle, uint32_t timeoutMs, String *out = nullptr) {
  String acc;                                     // 累积收到的所有文字
  uint32_t end = millis() + timeoutMs;            // 截止时间
  while (millis() < end) {
    while (McuSerial.available()) {
      acc += (char)McuSerial.read();              // 收一个字节，接在累积串后面
      if (acc.indexOf(needle) >= 0) {             // 出现目标词了？
        if (out) *out = acc;                      // 调用方要的话，把收到的内容带回去
        return true;                              // 立刻成功返回
      }
    }
    delay(2);
  }
  if (out) *out = acc;                            // 超时了，也把收到的东西交出去（方便排查）
  return false;                                   // 没等到
}

// 等一个"特定字节"（比 waitForText 更简单：只认一个字节，收到就成功）
// 举例：waitForByte('C', 20000) = "20 秒内，只要收到一个 'C' 就算成功"
bool waitForByte(uint8_t want, uint32_t timeoutMs) {
  uint32_t end = millis() + timeoutMs;
  while (millis() < end) {
    if (McuSerial.available()) {
      if ((uint8_t)McuSerial.read() == want) return true;  // 正好是想要的那个字节
    }
    delay(1);
  }
  return false;                                   // 超时
}

// ============================ 二、进 STM32 的命令行（抢那 5 秒窗口） ============================
// STM32 上电后只有 5 秒的"窗口期"：这 5 秒内收到 'w' 就进命令行，否则直接跳去跑应用。
// 所以我们：先复位 STM32 → 然后像敲门一样连敲 10 次 'w'（约 1.6 秒），总能撞进窗口。
bool enterMenu() {
  mcuReset();                             // 第一步：按复位键，让 STM32 重新开始那 5 秒倒计时
  uartDrain();                            // 第二步：把旧数据倒掉，免得干扰判断

  /* MCU 侧已改成“扫队列找 w”（任意位置都算），不用再狂发 40 次；
     10 次 × 160ms ≈ 1.6 秒，足够覆盖 MCU 启动 + 5 秒窗口内的前几次尝试。 */
  for (int i = 0; i < 10; i++) {
    McuSerial.write('w');                 // 喊一嗓子 "w"（只发一个字节！STM32 只认第一个字节）
    McuSerial.flush();                    // 等这个字节真的发完再继续（别发一半就跑）
    if (waitForText("[1]擦除A区", 160)) {  // 等菜单第一行：只有真正进了命令行才会打印
                                         //   （不能用"命令行"三字判断：STM32 上电提示里也有这三个字，会误判）
      waitForText("[8]清除OTA标志", 1500);// 接着把剩下的菜单文字也收完（等它打完，最多 1.5 秒）
      uartDrain();                        // 顺手把多余字符倒掉，保持干净
      return true;                        // 报告：成功进命令行
    }
  }
  return false;                           // 敲了 40 次都没人应 → 失败（接线/波特率/复位线可能有问题）
}

// ============================ 三、OTA 主体：把固件一帧帧发过去 ============================
// 走菜单 [2] 这条路：STM32 从串口直接收固件、写进 A 区 Flash
// data/len = 固件内容和长度；padByte = 最后一帧不满 128 字节时，用这个值补齐
bool iapDownload(uint8_t *data, size_t len, int padByte) {
  // ---- 第 1 步：进命令行 ----
  if (!enterMenu()) {                                   // 进不去就没什么好做的了
    appendLog("[!] 进不了命令行（检查复位线 / 波特率 / 接线）");
    return false;
  }

  // ---- 第 2 步：按 '2'，让 STM32 进入"串口 IAP"模式 ----
  McuSerial.write('2');                                 // 发菜单命令 2
  McuSerial.flush();                                    // 确保发出去
  if (!waitForText("请使用.bin格式文件", 5000)) {        // 等 STM32 回一句"请使用.bin格式文件"
    appendLog("[!] 菜单 [2] 无响应");                    // 5 秒没回话 → 失败
    return false;
  }
  appendLog("[*] 已进入 IAP 模式，等待 'C' …");

  // ---- 第 3 步：等 STM32 发来 'C'（'C' 的意思："我要用 CRC 校验模式，你开始发吧"）----
  if (!waitForByte('C', 20000)) {                       // 最多等 20 秒
    appendLog("[!] 没等到 'C'");
    return false;
  }

  // ---- 第 4 步：把固件切成 128 字节一块，一块一块发 ----
  size_t total = (len + 127) / 128;   // 总帧数 = 总字节数 ÷ 128，向上取整（比如 17020 字节 → 133 帧）
                                      //   为什么要 +127 再除：整数除法会向下取整，加 127 相当于"有零头也算一帧"
  uint8_t seq = 1;                    // Xmodem 的包号从 1 开始，每发成功一帧就 +1

  for (size_t i = 0; i < total; i++) {          // 一帧一帧地发
    uint8_t chunk[128];                         // 这一帧要发的 128 字节数据
    size_t n = len - i * 128;                   // 这一帧还剩多少字节没发（处理最后一帧不够 128 的情况）
    if (n > 128) n = 128;                       // 最多只能取 128
    memcpy(chunk, data + i * 128, n);           // 从固件里抠出这 128 字节
    for (size_t k = n; k < 128; k++) {          // 最后一帧可能不满 128
      chunk[k] = (uint8_t)padByte;              //   用 0xFF 补齐（空 Flash 就是 0xFF，填它最安全）
    }

    // ---- 把 133 字节的帧拼起来：这就是 Xmodem 的"信封格式" ----
    uint8_t frame[133];                         // 133 = 1(起始) + 2(包号) + 128(数据) + 2(校验)
    frame[0] = 0x01;                            // SOH：告诉对方"一帧开始了"
    frame[1] = seq;                             // 包号：1,2,3,...
    frame[2] = (uint8_t)(0xFF - seq);           // 包号的反码：对方用它校验包号有没有传错
                                                //   因为 seq + (0xFF-seq) 永远等于 0xFF
    memcpy(frame + 3, chunk, 128);              // 接上 128 字节数据
    uint16_t crc = crc16_xmodem(chunk, 128);    // 给这 128 字节算"指纹"
    frame[131] = (uint8_t)(crc >> 8);           // 指纹高字节在前（大端）
    frame[132] = (uint8_t)(crc & 0xFF);         // 指纹低字节在后

    // ---- 发出这一帧，并等对方的"回执" ----
    bool acked = false;                         // 对方确认收到的标志（ACK 才算成功）
    for (int retry = 0; retry < 8 && !acked; retry++) {   // 最多重试 8 次
      McuSerial.write(frame, 133);              // 一次把 133 字节全写出去
                                                //   必须"一次性"写出：STM32 靠"总线空闲"来切包，只认整帧
      McuSerial.flush();                        // 等发送完成
      uint32_t end = millis() + 3000;           // 给对方 3 秒时间回话
      while (millis() < end) {
        if (McuSerial.available()) {
          uint8_t r = (uint8_t)McuSerial.read();
          if (r == 0x06) {                      // 0x06 = ACK：收到了，很好
            acked = true;                       //   标记成功
            break;                              //   跳出等待
          }
          if (r == 0x15) {                      // 0x15 = NAK：这帧有问题，重发
            break;                              //   只跳出等待 → 外层 for 会再发一次同一帧
          }
        }
        delay(1);
      }
    }                                           // 重试 8 次还不 ACK 就往下走，由下面的 if 判错

    if (!acked) {                               // 8 次都没成功
      appendLog("[!] 第 " + String(i + 1) + " 帧重传失败");
      return false;                             // 整个升级失败
    }

    g_pct = (int)((i + 1) * 100 / total);       // 更新进度百分比（网页会来读它画进度条）
    seq++;                                      // 包号 +1，准备下一帧
    delay(2);                                   // 稍等一下，别把 STM32 逼太紧（它要写 Flash）
  }

  // ---- 第 5 步：发 EOT，告诉 STM32 "全部发完了" ----
  appendLog("[*] 发送 EOT");
  McuSerial.write((uint8_t)0x04);               // 0x04 = EOT（End Of Transmission）
  McuSerial.flush();
  waitForByte(0x06, 5000);                      // 等它回 ACK（5 秒内）
  delay(300);                                   // 等它把剩下的写到 Flash、软复位
  appendLog("[+] 升级完成，板子会自动复位并跳新程序");
  return true;                                  // 成功
}

// ============================ 四、后台任务：把耗时的升级放到"另一个工人"手里 ============================
// 为什么不直接在网页请求里做？因为升级要十几秒，会把网页"卡住"，手机一直转圈。
// 所以：网页请求只负责"开工"，真正干活的是这个独立任务（FreeRTOS 任务 ≈ 另一个线程）。
void upgradeTask(void *param) {
  g_busy = true;                                // 挂上"正在忙"的牌子
  g_pct = 0;                                    // 进度归零
  g_msg = "升级中";                             // 状态牌改成"升级中"
  bool ok = iapDownload(g_buf, g_len, 0xFF);    // 真正干活：一帧帧发固件（0xFF 是补齐用的填充值）
  g_msg = ok ? "升级成功" : "升级失败";          // 干完了，更新状态牌
  g_pct = ok ? 100 : g_pct;                     // 成功就把进度拉满
  g_busy = false;                               // 摘掉"正在忙"的牌子
  vTaskDelete(nullptr);                         // 工人任务结束，自己把自己注销（释放资源）
}

// ============================ 五、网页（整个"网站"就是下面这一段字符串） ============================
// PROGMEM = 把它放在 Flash 里，不占宝贵的 RAM（网页有 5KB 多，放 RAM 太浪费）
// R"HTML( ... )HTML" = C++ 的"原始字符串"：里面的引号、反斜杠都不用转义，写 HTML/JS 很方便
const char PAGE[] PROGMEM = R"HTML(
<!doctype html><html><head><meta charset="utf-8">
<!-- ↑ 声明这是 HTML 文档；charset=utf-8 保证中文不乱码 -->
<meta name="viewport" content="width=device-width,initial-scale=1">
<!-- ↑ 手机端按屏幕宽度缩放，不然字小得看不清 -->
<title>STM32 OTA 升级 v3</title>
<!-- ↑ 浏览器标签页上的标题；末尾的 v3 是"版本标记"，用来确认加载的是新页面（浏览器有缓存） -->
<style>
/* ===== 下面是"长相"部分：只影响外观，不影响功能 ===== */
body{font-family:-apple-system,"Microsoft YaHei",sans-serif;margin:0;padding:16px;background:#f5f6f8;color:#222}
h2{margin:6px 0 14px;font-size:19px}
.card{background:#fff;border-radius:12px;padding:14px;margin-bottom:12px;box-shadow:0 1px 4px rgba(0,0,0,.08)}
/* ↑ .card 是"卡片"样式：白底、圆角、淡阴影——页面上那几个方块区域就用它 */
button{font-size:15px;padding:10px 14px;margin:5px 6px 5px 0;border:0;border-radius:9px;background:#2e5496;color:#fff}
button.gray{background:#8a8f99}
/* ↑ 普通按钮＝蓝底白字；加了 class="gray" 的按钮＝灰底（表示"次要操作"） */
button:disabled{opacity:.5}
/* ↑ 按钮被禁用时半透明——升级过程中会禁用，防止重复点击 */
#bar{height:10px;background:#e3e6ec;border-radius:5px;overflow:hidden;margin-top:10px}
#fill{height:100%;width:0;background:#2e5496;transition:width .2s}
/* ↑ #bar 是进度条的"槽"，#fill 是里面会变长的"填充条"（宽度由 JS 改） */
#log{font-family:ui-monospace,Consolas,monospace;font-size:12px;white-space:pre-wrap;background:#0f1720;color:#b9f6ca;padding:10px;border-radius:9px;height:180px;overflow:auto}
/* ↑ #log 是那个黑色日志框：等宽字体、深色底、绿字、内容多了可滚动 */
input[type=file]{font-size:14px}
</style></head><body>
<!-- ===== 下面是"内容"部分：看得见的东西 ===== -->
<h2>STM32 OTA 升级（ESP32 桥）<span style='font-size:13px;color:#888'> v3</span></h2>

<div class="card">
  <!-- 状态卡片：id="st" 显示文字状态，id="pct" 显示百分比，下面那条是进度条 -->
  <div><b>状态：</b><span id="st">空闲</span>　<span id="pct"></span></div>
  <div id="bar"><div id="fill"></div></div>
</div>

<div class="card">
  <!-- 常用操作卡片：四个按钮，点击时调用 JS 的 cmd('x')，x 就是发给 STM32 的菜单字符 -->
  <div><b>常用操作</b></div>
  <button onclick="cmd('4')">读版本号</button>
  <button class="gray" onclick="cmd('1')">擦除 A 区</button>
  <button class="gray" onclick="cmd('8')">清除 OTA 标志</button>
  <button class="gray" onclick="cmd('7')">重启</button>
</div>

<div class="card">
  <!-- 升级卡片：file 输入框选文件；两个按钮分别走"上传+升级"和"只上传" -->
  <div><b>升级固件</b>（.bin，≤44KB）</div>
  <div style="margin:10px 0"><input type="file" id="f" accept=".bin"></div>
  <button id="up" onclick="doUpgrade()">上传并升级</button>
  <button class="gray" onclick="upOnly()">仅上传</button>
</div>

<div class="card"><div id="log">等待操作…</div></div>

<script>
/* ===== 下面是"行为"部分：按钮点了之后干什么 ===== */
const $=id=>document.getElementById(id);
/* ↑ 小工具：$('st') 就等于 document.getElementById('st')，少打字 */

function log(s){ $('log').textContent += s+"\n"; $('log').scrollTop=1e9; }
/* ↑ 往日志框追加一行；scrollTop=1e9 是让滚动条自动滚到最底下（1e9 就是"一个很大的数"） */

async function cmd(c){
  /* ↑ 点"读版本号/擦除/清标志/重启"时都会走这里：c 是那个菜单字符，比如 '4' */
  $('st').textContent='执行中…';                       // 状态先显示"执行中"
  try{
    const r=await fetch('/cmd?c='+c);                  // 向 ESP32 请求 /cmd?c=4
    const t=await r.text();                            // 拿到 ESP32 回的文字（STM32 的原始回话）
    log(t.trim()||('命令 '+c+' 完成'));                 // 有内容就显示内容，空的就显示"完成"
    $('st').textContent='空闲';                        // 状态恢复
  }catch(e){ log('错误: '+e); $('st').textContent='空闲'; }  // 出错也别忘了恢复状态
}

async function upOnly(){
  /* ↑ 只上传不升级：把选中的文件发给 ESP32 暂存起来 */
  const f=$('f').files[0];                             // 取用户选中的文件
  if(!f){ log('先选一个 .bin 文件'); return false; }    // 没选就提示并退出
  log('上传中：'+f.name+'（'+f.size+' 字节）…');
  const fd=new FormData();                             // 浏览器专门用来"打包文件"的容器
  fd.append('firmware', f, f.name);                    // 把文件塞进去，字段名 firmware
  const r=await fetch('/upload',{method:'POST',body:fd});  // POST 给 ESP32（文件必须用 POST）
  const t=await r.text();                              // ESP32 回"收到 N 字节"之类
  log(t);
  return r.ok;                                         // 上传成功返回 true，失败 false
}

async function doUpgrade(){
  /* ↑ "上传并升级"：先上传，再让 ESP32 开始刷 */
  $('up').disabled=true;                               // 先禁用按钮，防止中途又点一次
  try{
    if(!await upOnly()){ $('up').disabled=false; return; }  // 上传失败就恢复按钮并退出
    log('开始升级…（板子会先复位再进入 OTA）');
    await fetch('/upgrade', {method:'POST'});          // 告诉 ESP32："开始升级"（必须 POST！）
                                                       //   ↑ 这里踩过坑：早先写成 GET，服务器只认 POST，
                                                       //     结果升级函数一次都没被调用，页面立刻显示"结束：空闲"
    poll();                                            // 开始每 400ms 问一次进度
  }catch(e){ log('错误: '+e); $('up').disabled=false; } // 出错恢复按钮
}

let timer=null, pollT0=0;                              // timer=定时器句柄；pollT0=开始轮询的时刻
function poll(){
  pollT0=Date.now();                                   // 记下"开始轮询的时间"
  timer=setInterval(async()=>{                         // 每 400 毫秒执行一次括号里的内容
    const j=await (await fetch('/status')).json();     // 向 ESP32 要状态：{busy, pct, msg}
    $('st').textContent=j.msg;                         // 更新状态文字
    $('pct').textContent=j.busy? j.pct+'%':'';         // 忙的时候显示百分比
    $('fill').style.width=j.pct+'%';                   // 进度条宽度 = 百分比
    // 前 5 秒即使 busy=false 也继续轮询（任务刚创建时可能还没来得及把 busy 置 true）
    if(!j.busy && Date.now()-pollT0>5000){             // 不忙了、而且已经过了 5 秒
      clearInterval(timer);                            //   停掉定时器
      $('up').disabled=false;                          //   恢复按钮
      log('结束：'+j.msg);                              //   在日志里写结果
    }
  },400);                                              // 400ms 一轮
}
</script></body></html>
)HTML";

// ============================ 六、网页请求的处理函数（每个对应一个网址） ============================

// 访问 http://192.168.4.1/ 时：把上面那一大段 PAGE 发出去（send_P 直接发 Flash 里的内容，省 RAM）
void handleRoot() { server.send_P(200, "text/html", PAGE); }

// 访问 /status 时：回一段 JSON，告诉网页"忙不忙、进度多少、什么状态"
// JSON 就是这种格式：{"busy":false,"pct":0,"msg":"空闲"}
void handleStatus() {
  String j = "{\"busy\":" + String(g_busy ? "true" : "false") +   // 拼 JSON（引号要转义）
             ",\"pct\":" + String(g_pct) +
             ",\"msg\":\"" + g_msg + "\"}";
  server.send(200, "application/json", j);                        // 告诉浏览器"这是 JSON 数据"
}

// 访问 /cmd?c=4 时：进命令行 → 把字符 4 发给 STM32 → 把 STM32 的回话原样返回给网页
void handleCmd() {
  String c = server.arg("c");                          // 取出网址里的参数 c（比如 "4"）
  if (g_busy) { server.send(503, "text/plain", "忙，等升级结束"); return; }   // 正在升级就别插队
  if (c.length() != 1) { server.send(400, "text/plain", "参数 c 必须是单个字符"); return; }  // 只接受单字符命令
  g_busy = true;                                       // 挂上"忙"的牌子
  String out;
  if (enterMenu()) {                                   // ① 先抢进命令行
    uartDrain();                                       //    清掉残留
    McuSerial.write(c[0]);                             // ② 把菜单字符发过去
    McuSerial.flush();
    out = uartReadFor(1500);                           // ③ 守 1.5 秒，把 STM32 的回话收齐
  } else {
    out = "[!] 进不了命令行";                           //    没进去就把原因告诉网页
  }
  g_busy = false;                                      // 摘掉牌子
  server.send(200, "text/plain", out.length() ? out : "(无回显)");   // 回话；空的就说"无回显"
}

// 上传文件的"分块回调"：浏览器不是一次发完，而是每包 ~1KB 分好几次发，
// 所以这个函数会被反复调用三次状态：开始 → 写入（多次） → 结束
void handleUpload() {
  HTTPUpload &up = server.upload();                    // 拿到当前这次上传的信息
  if (up.status == UPLOAD_FILE_START) {                // ① 开始：申请内存、清零长度
    free(g_buf);                                       //    先释放上一次的旧货架
    g_buf = (uint8_t *)malloc(MAX_BIN);                //    申请一块 48KB 的内存当货架
    g_len = 0;                                         //    已收字节数清零
    appendLog("[*] 开始接收：" + up.filename + " 共 " + String(up.totalSize) + " 字节");
  } else if (up.status == UPLOAD_FILE_WRITE) {         // ② 写入：把这一小块接到货架后面
    if (g_buf && g_len + up.currentSize <= MAX_BIN) {  //    确认不会超过货架容量
      memcpy(g_buf + g_len, up.buf, up.currentSize);   //    接上去
      g_len += up.currentSize;                         //    已收长度增加
    }
  } else if (up.status == UPLOAD_FILE_END) {           // ③ 结束：记一笔
    appendLog("[*] 接收完成：" + String(g_len) + " 字节");
  }
}

// 上传全部结束后：告诉网页"收到了多少字节，可以升级"
void handleUploadDone() {
  if (!g_buf || g_len == 0) { server.send(500, "text/plain", "上传失败或文件为空"); return; }
  appendLog("[*] 收到固件 " + String(g_len) + " 字节");
  server.send(200, "text/plain", "收到 " + String(g_len) + " 字节，可以升级");
}

// 网页点"上传并升级"时：创建后台任务去刷，然后立刻回"已开始"（不阻塞网页）
void handleUpgradeStart() {
  if (g_busy) { server.send(503, "text/plain", "忙"); return; }             // 正在忙就拒绝
  if (!g_buf || g_len == 0) { server.send(400, "text/plain", "还没上传固件"); return; }  // 没固件就拒绝
  /* 先把状态置位再创建任务：否则前端 400ms 后第一次查询时任务还没被调度，
     busy 仍是 false → 页面会立刻显示“结束：空闲”（实测踩过）。 */
  g_busy = true;                                       // ① 先挂"忙"牌子（抢在前头）
  g_pct = 0;
  g_msg = "升级中";
  xTaskCreatePinnedToCore(upgradeTask, "ota", 8192, nullptr, 5, nullptr, 1);
  /* ↑ 创建 FreeRTOS 任务：函数=upgradeTask；名字="ota"；栈=8192 字节；
       参数=无；优先级=5；运行在核心 1 上（0 和 1 两个核，分开跑不抢） */
  server.send(200, "text/plain", "已开始");            // ② 立刻回复网页
}

// ============================ 七、程序入口 ============================
void setup() {
  // ---- ① 两个串口 ----
  Serial.begin(115200);                                // 调试串口：给你在电脑上看 ESP32 的输出
  McuSerial.begin(UART_BAUD, SERIAL_8N1, PIN_UART_RX, PIN_UART_TX);
  /* ↑ 和 STM32 说话的串口：921600 波特；8 数据位/无校验/1 停止位；RX=16 脚，TX=17 脚。
       注意参数顺序是"先 RX 后 TX"，和接线要对应上 */

  // ---- ② 复位线：平时拉高（STM32 的 NRST 是低电平复位，所以平时必须是高） ----
  if (USE_RESET) {
    pinMode(PIN_MCU_RST, OUTPUT_OPEN_DRAIN);   /* 开漏：平时高阻，不干扰 STM32 */
    digitalWrite(PIN_MCU_RST, HIGH);
  }

  // ---- ③ 开热点（相当于在自己身上架一个路由器） ----
  WiFi.mode(WIFI_AP);                                  // 设成"热点"模式（不是连别人）
  WiFi.softAP(AP_SSID, AP_PASS);                       // 开热点：名字 + 密码
  Serial.print("AP: ");
  Serial.println(AP_SSID);
  Serial.print("打开 http://");
  Serial.println(WiFi.softAPIP());                     // 打印热点自己的 IP（固定 192.168.4.1）

  // ---- ④ 注册路由：告诉服务器"哪个网址交给哪个函数"----
  server.on("/", HTTP_GET, handleRoot);                // 根路径 → 发网页
  server.on("/status", HTTP_GET, handleStatus);        // 查状态 → 回 JSON
  server.on("/cmd", HTTP_GET, handleCmd);              // 发命令 → 转串口
  server.on("/upload", HTTP_POST, handleUploadDone, handleUpload);
  /* ↑ 上传：最后一个参数是"分块回调"（边收边存），前一个是"收完后的处理" */
  server.on("/upgrade", HTTP_POST, handleUpgradeStart);// 开始升级 → 建后台任务
  server.begin();                                      // 服务器开始营业（之后才能接客）

  appendLog("[*] 桥已就绪，等手机连 " + String(AP_SSID) + " 后打开 http://192.168.4.1");
}

// 主循环：只做一件事——不停地"接待"网页请求（收到就分派给对应的 handleXxx）
void loop() {
  server.handleClient();
  /* ↑ 这一句是整台机器的"心脏"：没有它，网页请求永远没人应答。
     所有耗时的工作（升级）都被扔进 upgradeTask 了，所以这里能一直保持灵敏 */
}