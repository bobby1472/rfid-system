from datetime import datetime

from pydantic import BaseModel, ConfigDict, Field, field_validator


def normalise_uid(value: str) -> str:
    """ทำ UID ให้อยู่รูปเดียวกันเสมอ: ตัวพิมพ์ใหญ่ ไม่มี : - หรือช่องว่าง"""
    cleaned = value.replace(":", "").replace("-", "").replace(" ", "").strip().upper()
    if not cleaned:
        raise ValueError("UID ว่างไม่ได้")
    if not all(c in "0123456789ABCDEF" for c in cleaned):
        raise ValueError("UID ต้องเป็นเลขฐานสิบหกเท่านั้น")
    return cleaned


def lenient_uid(value: str | None) -> str | None:
    """ทำความสะอาด UID สำหรับใช้เป็นตัวกรองค้นหา

    ใช้กฎเดียวกับ normalise_uid แต่ไม่โยน error - ถ้าผู้ใช้พิมพ์อะไรที่ไม่ใช่
    เลขฐานสิบหก จะคืน None แปลว่า "ไม่มีทางตรงกับอะไรเลย" ดีกว่าตอบ 422 ใส่หน้าเขา
    ฝั่งที่เรียกต้องแยกกรณีนี้จากกรณีไม่ได้ส่งตัวกรองมาเลยด้วยตัวเอง
    """
    if value is None:
        return None
    try:
        return normalise_uid(value)
    except ValueError:
        return None


def normalise_mac(value: str | None) -> str | None:
    """จัดรูป MAC ให้เป็น AA:BB:CC:DD:EE:FF เสมอ

    เฟิร์มแวร์ส่งมาแบบมีโคลอนอยู่แล้ว แต่ถ้าวันหลังเปลี่ยนไปใช้ขีดหรือไม่มีตัวคั่น
    จะได้ไม่กลายเป็นคนละเครื่องกันในฐานข้อมูล
    """
    if value is None:
        return None
    raw = value.replace(":", "").replace("-", "").replace(".", "").strip().upper()
    if not raw:
        return None
    if len(raw) != 12 or not all(c in "0123456789ABCDEF" for c in raw):
        raise ValueError("MAC ต้องเป็นเลขฐานสิบหก 12 หลัก")
    return ":".join(raw[i:i + 2] for i in range(0, 12, 2))


def clean_optional(value: str | None) -> str | None:
    """ช่องว่างจากฟอร์มให้กลายเป็น NULL ไม่ใช่สตริงว่าง ไม่งั้น unique index จะชนกันเอง"""
    if value is None:
        return None
    trimmed = value.strip()
    return trimmed or None


# ---------- เครื่องอ่าน ----------


class ReaderBase(BaseModel):
    mac: str | None = Field(default=None, max_length=17, description="MAC ของบอร์ด เช่น D8:F1:5B:12:D0:B8")
    name: str | None = Field(default=None, max_length=120)
    location: str | None = Field(default=None, max_length=120)
    note: str | None = None
    active: bool = True


class ReaderCreate(ReaderBase):
    code: str = Field(min_length=1, max_length=32, description="รหัสเครื่อง เช่น WC09")

    @field_validator("code")
    @classmethod
    def _code(cls, v: str) -> str:
        return v.strip().upper()

    @field_validator("mac")
    @classmethod
    def _mac(cls, v: str | None) -> str | None:
        return normalise_mac(v)


class ReaderUpdate(BaseModel):
    mac: str | None = None
    name: str | None = None
    location: str | None = None
    note: str | None = None
    active: bool | None = None

    # ฟอร์มแก้ไขส่ง '' มาเวลาช่อง MAC ว่าง ต้องแปลงเป็น NULL ให้เหมือนตอนสร้าง
    # ไม่งั้น '' จะถูกเก็บลงไปตรง ๆ แล้วเครื่องสองตัวที่ไม่มี MAC จะชนกัน
    # ที่ unique index ขึ้น duplicate_mac ทั้งที่ไม่มี MAC ซ้ำจริง
    @field_validator("mac")
    @classmethod
    def _mac(cls, v: str | None) -> str | None:
        return normalise_mac(v)


class ReaderPage(BaseModel):
    items: list["ReaderOut"]
    total: int


class ReaderOut(ReaderBase):
    model_config = ConfigDict(from_attributes=True)

    id: int
    code: str
    registered: bool
    last_seen_at: datetime | None
    last_ip: str | None
    created_at: datetime
    updated_at: datetime


# ---------- บัตร ----------


class CardBase(BaseModel):
    holder_name: str = Field(min_length=1, max_length=120)
    barcode: str | None = Field(default=None, max_length=64)
    card_type: str | None = Field(default=None, max_length=60)
    note: str | None = None
    active: bool = True

    @field_validator("barcode", "card_type")
    @classmethod
    def _optional(cls, v: str | None) -> str | None:
        return clean_optional(v)


class CardCreate(CardBase):
    uid: str

    @field_validator("uid")
    @classmethod
    def _uid(cls, v: str) -> str:
        return normalise_uid(v)


class CardUpdate(BaseModel):
    holder_name: str | None = Field(default=None, min_length=1, max_length=120)
    barcode: str | None = None
    card_type: str | None = None
    note: str | None = None
    active: bool | None = None

    @field_validator("barcode", "card_type")
    @classmethod
    def _optional(cls, v: str | None) -> str | None:
        return clean_optional(v)


class CardPage(BaseModel):
    items: list["CardOut"]
    total: int


class CardOut(CardBase):
    model_config = ConfigDict(from_attributes=True)

    id: int
    uid: str
    created_at: datetime
    updated_at: datetime


# ---------- การแตะบัตร ----------


class ScanIn(BaseModel):
    """สิ่งที่ ESP8266 ส่งมาตอนแตะบัตร"""

    uid: str
    card_type: str | None = None
    device_id: str | None = Field(default=None, max_length=32, description="รหัสเครื่องอ่าน เช่น WC09")
    mac: str | None = Field(default=None, max_length=17, description="MAC ของบอร์ดที่ส่งมา")

    @field_validator("uid")
    @classmethod
    def _uid(cls, v: str) -> str:
        return normalise_uid(v)

    @field_validator("device_id")
    @classmethod
    def _device(cls, v: str | None) -> str | None:
        v = clean_optional(v)
        return v.upper() if v else None

    @field_validator("mac")
    @classmethod
    def _mac(cls, v: str | None) -> str | None:
        return normalise_mac(v)


class AnnounceIn(BaseModel):
    """อุปกรณ์ประกาศตัวเองตอนบูตและเป็นระยะ ๆ

    แยกจาก /api/scan เพราะต้องให้เครื่องโผล่ในหน้าเว็บได้
    ตั้งแต่ก่อนมีใครเอาบัตรมาแตะ
    """

    device_id: str = Field(min_length=1, max_length=32)
    mac: str | None = Field(default=None, max_length=17)
    firmware: str | None = Field(default=None, max_length=32)

    @field_validator("device_id")
    @classmethod
    def _device(cls, v: str) -> str:
        return v.strip().upper()

    @field_validator("mac")
    @classmethod
    def _mac(cls, v: str | None) -> str | None:
        return normalise_mac(v)


class AnnounceOut(BaseModel):
    code: str
    registered: bool
    name: str | None
    known_ip: str | None


class ScanOut(BaseModel):
    """ตอบกลับให้ ESP8266 - จำกัด 16 ตัวอักษรเพราะจอ LCD กว้างเท่านั้น"""

    allowed: bool
    name: str = Field(max_length=16)
    # บาร์โค้ดของบัตร ว่างได้ถ้าบัตรยังไม่ได้ผูกบาร์โค้ดไว้ หรือเป็นบัตรที่ไม่รู้จัก
    # เฟิร์มแวร์เอาไปโชว์บรรทัดล่างของจอตอน ACCESS OK
    barcode: str = Field(default="", max_length=16)
    message: str = Field(max_length=16)
    log_id: int


# ---------- ประวัติ ----------


class LogOut(BaseModel):
    model_config = ConfigDict(from_attributes=True)

    id: int
    uid: str
    card_id: int | None
    reader_id: int | None
    holder_name: str | None
    barcode: str | None
    card_type: str | None
    reader_code: str | None
    allowed: bool
    reason: str
    scanned_at: datetime


class LogPage(BaseModel):
    items: list[LogOut]
    total: int


class Stats(BaseModel):
    total_cards: int
    active_cards: int
    total_readers: int
    scans_today: int
    denied_today: int


# ประกาศ *Page ไว้ก่อน *Out จึงต้องผูก forward reference ให้เรียบร้อยตอนท้ายไฟล์
ReaderPage.model_rebuild()
CardPage.model_rebuild()
