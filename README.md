# RFID System

ระบบอ่านบัตร RFID ครบวงจร: เครื่องอ่าน NodeMCU + RC522 ส่งข้อมูลผ่าน WiFi
เข้าเซิร์ฟเวอร์ แล้วตรวจสอบสิทธิ์กับฐานข้อมูล PostgreSQL พร้อมหน้าเว็บจัดการ

```
NodeMCU + RC522  ──WiFi/HTTP──>  FastAPI  ──>  PostgreSQL
     (WC09)                          ^
                                     |
                              React (Vite) หน้าเว็บจัดการ
```

## โครงสร้าง

| โฟลเดอร์ | คืออะไร |
|---|---|
| `backend/` | FastAPI + SQLAlchemy async + asyncpg |
| `frontend/` | React 18 + Vite |
| `firmware/RFID_Station_WiFi/` | โค้ด Arduino สำหรับ NodeMCU |

## ตารางในฐานข้อมูล

**`cards`** — บัตรที่ลงทะเบียน
`uid`, `barcode`, `holder_name`, `card_type`, `note`, `active`

**`readers`** — เครื่องอ่านแต่ละตัว
`code` (WC05/WC07/WC08/WC09), `name`, `location`, `active`, `last_seen_at`, `last_ip`

**`access_logs`** — ทุกครั้งที่มีการแตะบัตร
`uid`, `barcode`, `reader_code`, `scanned_at`, `holder_name`, `card_type`, `allowed`, `reason`

> ค่าอย่างชื่อและบาร์โค้ดถูกคัดลอกเก็บไว้ใน log ด้วย เพื่อให้ประวัติยังอ่านรู้เรื่อง
> แม้บัตรจะถูกแก้ชื่อหรือลบทิ้งภายหลัง

---

## ติดตั้ง

### 1. ติดตั้งแพ็กเกจของ backend

```
cd backend
python -m venv .venv
.venv\Scripts\python.exe -m pip install -r requirements.txt
```

ต้องติดตั้งจาก `requirements.txt` ทั้งไฟล์ — `greenlet` และ `tzdata` จำเป็นจริง
(`greenlet` ใช้โดย SQLAlchemy ฝั่ง async, `tzdata` ใช้โดย zoneinfo บน Windows)
ถ้าติดตั้งเองทีละตัวจะเจอ error `the greenlet library is required`

### 2. ฐานข้อมูล

ใส่รหัสผ่าน PostgreSQL ลงในไฟล์ `backend/.env` บรรทัด `POSTGRES_PASSWORD=`
และตรวจว่า `APP_TIMEZONE` ตรงกับเขตเวลาหน้างาน (ค่าเริ่มต้น `Asia/Bangkok`)
ค่านี้ใช้ตัดสินว่า "วันนี้" เริ่มกี่โมงในหน้าสถิติ

จากนั้นสร้างฐานข้อมูลและตาราง:

```
.venv\Scripts\python.exe setup_db.py
```

สคริปต์จะสร้างฐานข้อมูลชื่อ `rfid-system` ให้เอง ถ้ามีอยู่แล้วก็จะข้ามไป

### 3. เริ่ม backend

```
cd backend
.venv\Scripts\python.exe -m uvicorn app.main:app --host 0.0.0.0 --port 7777 --reload
```

เปิดเอกสาร API อัตโนมัติได้ที่ http://localhost:7777/docs

### 4. เริ่ม frontend

```
cd frontend
npm run dev
```

เปิดหน้าเว็บที่ http://localhost:9910 

### 5. เฟิร์มแวร์

แก้ `firmware/RFID_Station_WiFi/config.h`:

```c
#define WIFI_SSID      "ชื่อ WiFi"
#define WIFI_PASSWORD  "รหัสผ่าน"
#define API_HOST       "192.168.1.100"   // IP ของเครื่องที่รัน backend
#define DEVICE_ID      "WC09"            // รหัสเครื่องอ่านตัวนี้
```

หา IP ของเครื่องด้วยคำสั่ง `ipconfig` แล้วดูค่า IPv4 Address
**ห้ามใส่ `localhost`** เพราะ localhost ของ ESP8266 คือตัวมันเอง

เปิดไฟล์ `.ino` ใน Arduino IDE แล้วอัปโหลด

### 6. เปิด Firewall ให้ ESP8266 เข้าถึงได้

Windows Firewall บล็อกพอร์ตขาเข้าเป็นค่าเริ่มต้น ถ้าไม่เปิด เครื่องอ่านจะขึ้น
`NO SERVER` ตลอด รันคำสั่งนี้ใน **PowerShell แบบ Run as Administrator**:

```
New-NetFirewallRule -DisplayName "RFID System API 7777" -Direction Inbound -Protocol TCP -LocalPort 7777 -Action Allow -Profile Private
```

---

## การต่อสาย

| อุปกรณ์ | ขา NodeMCU |
|---|---|
| RC522 SDA/SS | D8 |
| RC522 SCK | D5 |
| RC522 MOSI | D7 |
| RC522 MISO | D6 |
| RC522 RST | ไม่ต่อ |
| RC522 VCC | 3V3 |
| LCD SDA | D2 |
| LCD SCL | D1 |
| LCD VCC | **VIN (5V)** — บอร์ด LoLin ใช้ **VU** |
| Buzzer I/O | D0 |
| Buzzer VCC | **VIN (5V)** — บอร์ด LoLin ใช้ **VU** |
| LED 5mm ขายาว | 3V3 ผ่านตัวต้านทาน 220Ω |
| LED 5mm ขาสั้น | **D4** |
| GND ทั้งหมด | GND |

จอ LCD และบัซเซอร์ต้องใช้ไฟ 5V จาก `VIN` — ที่ 3.3V จอจะไม่แสดงตัวอักษรและบัซเซอร์จะไม่ดัง

### บอร์ด 2 รุ่นใช้ขาไฟ 5V ต่างกัน

ดูชิปสี่เหลี่ยมเล็ก ๆ ใกล้หัว USB ด้านหลังบอร์ด:

| ชิป USB | รุ่นบอร์ด | ขาจ่าย 5V ให้จอ + บัซเซอร์ |
|---|---|---|
| CP2102 | NodeMCU v2 / Amica (บอร์ดแคบ) | `VIN` |
| CH340 | NodeMCU v3 / LoLin (บอร์ดกว้าง) | **`VU`** — ขา VIN ของรุ่นนี้ไม่ได้จ่าย 5V จาก USB ออกมา |

ถ้าต่อผิดขา จอจะดับสนิทไม่มีไฟพื้นหลังเลย และบัซเซอร์จะไม่ดัง ขาอื่นทั้งหมด (D0–D8, 3V3, GND) ใช้ตำแหน่งเดียวกันทั้งสองรุ่น

LED ต่อ**กลับทิศจากปกติ** คือขาสั้นเข้า D4 ไม่ใช่ลง GND เพราะ D4 (GPIO2) ต้องเป็น HIGH ตอนบูต
ถ้าต่อ LED ลง GND บอร์ดอาจไม่บูต หลอดจะติดพร้อมไฟสีน้ำเงินบนโมดูลเสมอ
ส่วนการต่อระหว่าง 3V3 กับ GND ตรง ๆ จะติดค้างตลอดเพราะไม่ได้ผ่านขาที่โค้ดสั่งได้

---

## พฤติกรรมของเครื่องอ่าน

| สถานการณ์ | จอ LCD | เสียง |
|---|---|---|
| บัตรผ่าน | `ACCESS OK` + ชื่อผู้ถือ | บี๊บสั้น 1 ครั้ง |
| บัตรไม่ผ่าน | `DENIED <UID>` + ชื่อหรือ `Unknown card` | บี๊บยาว 2 ครั้ง |
| WiFi หลุด | `NO WIFI` + UID | บี๊บรัว 4 ครั้ง |
| ต่อเซิร์ฟเวอร์ไม่ได้ | `NO SERVER` + UID | บี๊บรัว 5 ครั้ง |
| RC522 ไม่ตอบตอนบูต | `RC522 ERROR / retrying...` | บี๊บยาว 3 ครั้ง |

จอจะค้างผลของบัตรใบล่าสุดไว้จนกว่าจะมีบัตรใบใหม่มาแตะ

กรณี `DENIED` จะแสดง UID บนจอด้วย เพื่อเอาไปเทียบกับ UID ที่ขึ้นบนหน้าเว็บได้

เมื่อติดต่อเซิร์ฟเวอร์ไม่ได้ ระบบจะ**ปฏิเสธไว้ก่อน** เพื่อความปลอดภัย

ถ้า RC522 ไม่ตอบตอนเปิดเครื่อง (เกิดได้เวลาเสียบปลั๊กสวิตช์ ไฟ 3.3V ขึ้นช้า)
เครื่องจะลอง init ใหม่ทุก 3 วินาทีและต่อ WiFi ไปพร้อมกัน ไม่ค้างตายรอคนไปกดรีเซ็ต

## เหตุผลที่ปฏิเสธ (คอลัมน์ reason)

| ค่า | ความหมาย |
|---|---|
| `ok` | ผ่าน |
| `unknown_card` | UID นี้ยังไม่ได้ลงทะเบียน |
| `card_disabled` | บัตรถูกปิดใช้งาน |
| `reader_disabled` | เครื่องอ่านถูกปิดใช้งานจากหน้าเว็บ |

---

## หมายเหตุด้านข้อมูลส่วนบุคคล

ระบบนี้เก็บชื่อผู้ถือบัตรและประวัติการเข้าออก ซึ่งเป็นข้อมูลส่วนบุคคล
ใช้ภายในองค์กรเท่านั้น ไฟล์ `.env` ที่มีรหัสผ่านถูกใส่ไว้ใน `.gitignore` แล้ว
