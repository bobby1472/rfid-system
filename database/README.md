# Database scripts

สคริปต์ SQL สำหรับสร้างฐานข้อมูล `rfid-system` บน PostgreSQL โดยไม่ต้องใช้ Python

| ไฟล์ | ใช้ทำอะไร | ต่อกับฐานข้อมูล |
|---|---|---|
| `01_create_database.sql` | สร้างฐานข้อมูล `rfid-system` (UTF8) | `postgres` |
| `02_schema.sql` | สร้างตาราง `cards`, `readers`, `access_logs` พร้อมดัชนี | `rfid-system` |
| `03_migrate_readers_mac.sql` | เพิ่มคอลัมน์ `mac`, `registered` ให้ฐานข้อมูลรุ่นเก่า | `rfid-system` |

ทุกไฟล์ยกเว้น `01` **รันซ้ำได้ปลอดภัย** ของเดิมไม่หาย

## ติดตั้งใหม่

รัน `01` แล้วตามด้วย `02` ไม่ต้องรัน `03` เพราะ `02` มีคอลัมน์ครบอยู่แล้ว

### ผ่าน psql

psql อยู่ที่ `D:\Program Files\PostgreSQL\18\bin\psql.exe` บนเครื่องเซิร์ฟเวอร์ จะถามรหัสผ่านของ `postgres` ทุกครั้ง

```
cd D:\Arduino\rfid-system\database
"D:\Program Files\PostgreSQL\18\bin\psql.exe" -U postgres -d postgres    -f 01_create_database.sql
"D:\Program Files\PostgreSQL\18\bin\psql.exe" -U postgres -d rfid-system -f 02_schema.sql
```

### ผ่าน pgAdmin

1. คลิกขวาที่ฐานข้อมูล `postgres` > **Query Tool** > เปิดไฟล์ `01_create_database.sql` > รัน
2. รีเฟรชรายการฐานข้อมูล จะเห็น `rfid-system`
3. คลิกขวาที่ `rfid-system` > **Query Tool** > เปิดไฟล์ `02_schema.sql` > รัน

ต้องเปิด Query Tool **จากฐานข้อมูลที่ถูกต้อง** ตามคอลัมน์ขวาสุดของตารางด้านบน ถ้ารัน `02` ตอนต่อกับ `postgres`
ตารางจะไปอยู่ผิดฐานข้อมูล แอปจะหาไม่เจอ

### ผ่าน Python

`backend/setup_db.py` ทำแบบเดียวกับ `01` + `02` + `03` รวมกันในคำสั่งเดียว อ่านรหัสผ่านจาก `backend/.env`

```
cd D:\Arduino\rfid-system\backend
.venv\Scripts\python.exe setup_db.py
```

## อัปเกรดฐานข้อมูลที่ใช้งานอยู่

ถ้าฐานข้อมูลสร้างไว้ก่อนมีฟีเจอร์ MAC address ให้รัน `03`:

```
"D:\Program Files\PostgreSQL\18\bin\psql.exe" -U postgres -d rfid-system -f 03_migrate_readers_mac.sql
```

ปกติไม่ต้องรันเอง เพราะ backend รัน migration ชุดเดียวกันให้ทุกครั้งที่สตาร์ท (`backend/app/migrate.py`)
ไฟล์นี้มีไว้สำหรับเตรียมฐานข้อมูลก่อนเปิดแอป หรือให้ DBA ตรวจดูก่อนนำไปใช้

`03` จะตั้งเครื่องอ่านที่มีอยู่เดิมทุกเครื่องเป็น `registered = true` เฉพาะครั้งแรกที่เพิ่มคอลัมน์
รันซ้ำจะไม่ไปเปลี่ยนเครื่องที่เพิ่งถูกค้นพบ ซึ่งยังรอกด Add reader อยู่

## ⚠️ ข้อความภาษาไทยกับ psql บน Windows

**อย่าส่งคำสั่งที่มีภาษาไทยผ่าน `psql -c`** Windows จะแปลงอาร์กิวเมนต์บน command line เป็น codepage 1252
ซึ่งไม่มีตัวอักษรไทย ข้อความจะถูกบันทึกเป็น `???` **โดยไม่มี error แจ้งเลย**

ใช้วิธีเหล่านี้แทน
- เขียน SQL ลงไฟล์ที่บันทึกเป็น UTF-8 แล้วรันด้วย `psql -f`
- ใช้ pgAdmin
- ใช้หน้าเว็บของระบบ

สคริปต์ทุกไฟล์ในโฟลเดอร์นี้ขึ้นต้นด้วย `SET client_encoding = 'UTF8';` จึงรันผ่าน `-f` ได้ถูกต้อง

## เมื่อแก้โครงสร้างตาราง

ไฟล์เหล่านี้ต้องตรงกับ `backend/app/models.py` เสมอ ถ้าเพิ่มคอลัมน์ในโมเดลต้องแก้ 2 ที่

1. เพิ่มคอลัมน์ใน `02_schema.sql` สำหรับการติดตั้งใหม่
2. ทำไฟล์ migrate ใหม่ (เช่น `04_...sql`) และเพิ่มใน `backend/app/migrate.py` สำหรับฐานข้อมูลที่ใช้งานอยู่

ต้องทำข้อ 2 ด้วยเสมอ เพราะ `CREATE TABLE IF NOT EXISTS` จะข้ามตารางที่มีอยู่แล้วทั้งตาราง ไม่เติมคอลัมน์ใหม่ให้

สคริปต์ชุดนี้ทดสอบแล้วโดยรันลงฐานข้อมูลชั่วคราว แล้วเทียบกับฐานข้อมูลจริงที่แอปสร้างขึ้น
ผลคือคอลัมน์ 32 ตัว ดัชนี 8 ตัว และ foreign key 2 ตัวตรงกันทั้งหมด
