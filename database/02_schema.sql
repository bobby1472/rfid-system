-- =====================================================================
-- 02  ตารางและดัชนีทั้งหมดของ rfid-system
-- =====================================================================
--
-- รันตอนต่ออยู่กับฐานข้อมูล "rfid-system" แล้ว
-- รันซ้ำได้ปลอดภัย ทุกคำสั่งใช้ IF NOT EXISTS ของเดิมไม่หาย
--
-- โครงสร้างนี้ต้องตรงกับ backend/app/models.py เสมอ
-- ถ้าแก้โมเดลแล้วให้แก้ไฟล์นี้ตาม และถ้าเป็นการเพิ่มคอลัมน์ให้ฐานข้อมูลที่ใช้งานอยู่
-- ต้องทำไฟล์ migrate แยกด้วย (ดู 03_migrate_readers_mac.sql เป็นตัวอย่าง)
-- เพราะ CREATE TABLE IF NOT EXISTS จะข้ามตารางที่มีอยู่แล้วทั้งตาราง ไม่เติมคอลัมน์ให้
--
-- ลำดับสำคัญ: cards กับ readers ต้องมาก่อน access_logs เพราะ access_logs อ้างถึงทั้งสองตาราง

SET client_encoding = 'UTF8';

BEGIN;

-- ---------------------------------------------------------------------
-- cards : บัตรที่ลงทะเบียนไว้ในระบบ
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS cards (
    id           SERIAL        PRIMARY KEY,
    -- UID เก็บเป็นตัวพิมพ์ใหญ่ ไม่มีตัวคั่น เช่น F3272907 (backend จัดรูปให้ก่อนบันทึก)
    uid          VARCHAR(32)   NOT NULL,
    -- บาร์โค้ดที่พิมพ์อยู่บนตัวบัตร ว่างได้ Postgres ยอมให้ NULL ซ้ำกันได้แม้มี unique
    barcode      VARCHAR(64),
    holder_name  VARCHAR(120)  NOT NULL,
    card_type    VARCHAR(60),
    note         TEXT,
    -- ปิดใช้งานแทนการลบ ประวัติการเข้าออกจะได้ไม่ขาด
    active       BOOLEAN       NOT NULL DEFAULT true,
    created_at   TIMESTAMPTZ   NOT NULL DEFAULT now(),
    updated_at   TIMESTAMPTZ   NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS ix_cards_uid     ON cards (uid);
CREATE UNIQUE INDEX IF NOT EXISTS ix_cards_barcode ON cards (barcode);

COMMENT ON TABLE  cards         IS 'Registered RFID cards';
COMMENT ON COLUMN cards.uid     IS 'Card UID, uppercase hex without separators';
COMMENT ON COLUMN cards.barcode IS 'Barcode printed on the card; NULL when not assigned';
COMMENT ON COLUMN cards.active  IS 'false = card rejected at every reader';


-- ---------------------------------------------------------------------
-- readers : เครื่องอ่าน RC522 แต่ละตัว เช่น WC05, WC07, WC09
-- ---------------------------------------------------------------------
CREATE TABLE IF NOT EXISTS readers (
    id            SERIAL        PRIMARY KEY,
    -- รหัสที่เฟิร์มแวร์ส่งมาใน device_id ตรงกับ DEVICE_ID ใน config.h
    code          VARCHAR(32)   NOT NULL,
    -- MAC ของบอร์ด ติดกับฮาร์ดแวร์ถาวร ไม่เปลี่ยนตาม DHCP เหมือน IP
    mac           VARCHAR(17),
    -- false = บอร์ดประกาศตัวเข้ามาเองแต่ยังไม่มีคนกด Add reader ในหน้าเว็บ
    registered    BOOLEAN       NOT NULL DEFAULT false,
    name          VARCHAR(120),
    location      VARCHAR(120),
    note          TEXT,
    active        BOOLEAN       NOT NULL DEFAULT true,
    -- อัปเดตทุกครั้งที่บอร์ดยิงข้อมูลเข้ามา ทั้งตอนแตะบัตรและตอนประกาศตัวทุก 5 นาที
    last_seen_at  TIMESTAMPTZ,
    last_ip       VARCHAR(45),
    created_at    TIMESTAMPTZ   NOT NULL DEFAULT now(),
    updated_at    TIMESTAMPTZ   NOT NULL DEFAULT now()
);

CREATE UNIQUE INDEX IF NOT EXISTS ix_readers_code ON readers (code);
CREATE UNIQUE INDEX IF NOT EXISTS ix_readers_mac  ON readers (mac);

COMMENT ON TABLE  readers            IS 'RFID reader stations (NodeMCU + RC522)';
COMMENT ON COLUMN readers.code       IS 'Station code sent by the firmware as device_id, e.g. WC09';
COMMENT ON COLUMN readers.mac        IS 'Board MAC address, AA:BB:CC:DD:EE:FF';
COMMENT ON COLUMN readers.registered IS 'false = discovered on the network but not yet confirmed in the web app';
COMMENT ON COLUMN readers.active     IS 'false = this reader rejects every card';


-- ---------------------------------------------------------------------
-- access_logs : ทุกครั้งที่มีการแตะบัตร ไม่ว่าจะผ่านหรือไม่ผ่าน
-- ---------------------------------------------------------------------
-- holder_name, barcode, card_type, reader_code เป็นค่า ณ เวลาที่แตะ
-- ไม่ได้ดึงผ่าน foreign key เพื่อให้ประวัติยังอ่านรู้เรื่องแม้บัตรหรือเครื่องถูกแก้หรือลบทีหลัง
CREATE TABLE IF NOT EXISTS access_logs (
    id           SERIAL        PRIMARY KEY,
    uid          VARCHAR(32)   NOT NULL,
    -- ลบบัตรหรือเครื่องแล้วประวัติต้องอยู่ต่อ จึงตั้งเป็น SET NULL ไม่ใช่ CASCADE
    card_id      INTEGER       REFERENCES cards (id)   ON DELETE SET NULL,
    reader_id    INTEGER       REFERENCES readers (id) ON DELETE SET NULL,
    holder_name  VARCHAR(120),
    barcode      VARCHAR(64),
    card_type    VARCHAR(60),
    reader_code  VARCHAR(32),
    allowed      BOOLEAN       NOT NULL,
    -- ok | unknown_card | card_disabled | reader_disabled
    reason       VARCHAR(40)   NOT NULL,
    scanned_at   TIMESTAMPTZ   NOT NULL DEFAULT now()
);

CREATE INDEX IF NOT EXISTS ix_access_logs_uid             ON access_logs (uid);
CREATE INDEX IF NOT EXISTS ix_access_logs_reader_code     ON access_logs (reader_code);
CREATE INDEX IF NOT EXISTS ix_access_logs_scanned_at      ON access_logs (scanned_at);
-- หน้าประวัติเรียงจากใหม่ไปเก่าเสมอ ดัชนีแบบ DESC ช่วยตอนข้อมูลเยอะ
CREATE INDEX IF NOT EXISTS ix_access_logs_scanned_at_desc ON access_logs (scanned_at DESC);

COMMENT ON TABLE  access_logs             IS 'Every card tap, allowed or denied';
COMMENT ON COLUMN access_logs.holder_name IS 'Snapshot at scan time, kept even if the card is later edited or deleted';
COMMENT ON COLUMN access_logs.barcode     IS 'Snapshot at scan time';
COMMENT ON COLUMN access_logs.reader_code IS 'Snapshot at scan time';
COMMENT ON COLUMN access_logs.reason      IS 'ok | unknown_card | card_disabled | reader_disabled';

COMMIT;
