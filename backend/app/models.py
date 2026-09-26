from datetime import datetime

from sqlalchemy import Boolean, DateTime, ForeignKey, Index, String, Text, func
from sqlalchemy.orm import Mapped, mapped_column, relationship

from .database import Base


class Reader(Base):
    """เครื่องอ่าน RC522 แต่ละตัว เช่น WC05, WC07, WC08, WC09"""

    __tablename__ = "readers"

    id: Mapped[int] = mapped_column(primary_key=True)
    # รหัสที่เฟิร์มแวร์ส่งมาใน device_id - เป็นตัวระบุตัวจริงของเครื่อง
    code: Mapped[str] = mapped_column(String(32), unique=True, index=True)
    # MAC ของบอร์ด ติดกับฮาร์ดแวร์ถาวร ต่างจาก IP ที่ DHCP เปลี่ยนได้
    # ใช้ยืนยันว่าเป็นบอร์ดตัวเดิมจริงแม้จะย้ายที่หรือได้ IP ใหม่
    mac: Mapped[str | None] = mapped_column(String(17), unique=True, index=True, default=None)
    # อุปกรณ์ที่ยิงเข้ามาเองจะถูกสร้างด้วย registered=False ("พบอุปกรณ์ใหม่")
    # จะกลายเป็น True เมื่อคนกด Add reader ยืนยันในหน้าเว็บ
    registered: Mapped[bool] = mapped_column(Boolean, default=False, server_default="false")
    name: Mapped[str | None] = mapped_column(String(120), default=None)
    location: Mapped[str | None] = mapped_column(String(120), default=None)
    note: Mapped[str | None] = mapped_column(Text, default=None)
    active: Mapped[bool] = mapped_column(Boolean, default=True, server_default="true")

    # อัปเดตทุกครั้งที่เครื่องยิงข้อมูลเข้ามา ใช้ดูว่าเครื่องไหนยังออนไลน์
    last_seen_at: Mapped[datetime | None] = mapped_column(DateTime(timezone=True), default=None)
    last_ip: Mapped[str | None] = mapped_column(String(45), default=None)

    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    logs: Mapped[list["AccessLog"]] = relationship(back_populates="reader")


class Card(Base):
    """บัตรที่ลงทะเบียนไว้ในระบบ"""

    __tablename__ = "cards"

    id: Mapped[int] = mapped_column(primary_key=True)
    # UID เก็บเป็นตัวพิมพ์ใหญ่ไม่มีคั่น เช่น F3272907
    uid: Mapped[str] = mapped_column(String(32), unique=True, index=True)
    # บาร์โค้ดที่พิมพ์อยู่บนตัวบัตร - Postgres ยอมให้ NULL ซ้ำกันได้ จึงใส่ unique ตรง ๆ ได้
    barcode: Mapped[str | None] = mapped_column(String(64), unique=True, index=True, default=None)
    holder_name: Mapped[str] = mapped_column(String(120))
    card_type: Mapped[str | None] = mapped_column(String(60), default=None)
    note: Mapped[str | None] = mapped_column(Text, default=None)
    # ปิดการใช้งานบัตรได้โดยไม่ต้องลบทิ้ง ประวัติจะได้ไม่ขาด
    active: Mapped[bool] = mapped_column(Boolean, default=True, server_default="true")

    created_at: Mapped[datetime] = mapped_column(DateTime(timezone=True), server_default=func.now())
    updated_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), onupdate=func.now()
    )

    logs: Mapped[list["AccessLog"]] = relationship(back_populates="card")


class AccessLog(Base):
    """ทุกครั้งที่มีการแตะบัตร ไม่ว่าจะผ่านหรือไม่ผ่าน"""

    __tablename__ = "access_logs"

    id: Mapped[int] = mapped_column(primary_key=True)
    uid: Mapped[str] = mapped_column(String(32), index=True)
    # nullable เพราะบัตรที่ไม่รู้จักก็ต้องถูกบันทึกเหมือนกัน
    card_id: Mapped[int | None] = mapped_column(ForeignKey("cards.id", ondelete="SET NULL"), default=None)
    reader_id: Mapped[int | None] = mapped_column(ForeignKey("readers.id", ondelete="SET NULL"), default=None)

    # ค่า ณ เวลานั้น เผื่อบัตรหรือเครื่องถูกแก้ชื่อหรือลบทีหลัง ประวัติยังอ่านรู้เรื่อง
    holder_name: Mapped[str | None] = mapped_column(String(120), default=None)
    barcode: Mapped[str | None] = mapped_column(String(64), default=None)
    card_type: Mapped[str | None] = mapped_column(String(60), default=None)
    reader_code: Mapped[str | None] = mapped_column(String(32), index=True, default=None)

    allowed: Mapped[bool] = mapped_column(Boolean)
    reason: Mapped[str] = mapped_column(String(40))

    scanned_at: Mapped[datetime] = mapped_column(
        DateTime(timezone=True), server_default=func.now(), index=True
    )

    card: Mapped["Card | None"] = relationship(back_populates="logs")
    reader: Mapped["Reader | None"] = relationship(back_populates="logs")


Index("ix_access_logs_scanned_at_desc", AccessLog.scanned_at.desc())
