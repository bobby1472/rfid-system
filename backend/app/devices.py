"""ตรรกะกลางเรื่อง "เห็นอุปกรณ์ตัวนี้แล้ว"

ใช้ร่วมกันระหว่าง /api/scan (ตอนแตะบัตร) กับ /api/readers/announce (ตอนบูต)
ถ้าแยกกันเขียน สองทางจะบันทึก MAC/IP ไม่เหมือนกันแล้วข้อมูลเพี้ยน
"""
from datetime import datetime, timezone

from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from .models import Reader


async def _release_mac(session: AsyncSession, mac: str, keep_code: str) -> None:
    """ปลด MAC ออกจากแถวอื่นที่ยังถืออยู่ ก่อนจะเอาไปใส่ให้แถวที่ถูกต้อง

    MAC เป็นของบอร์ดตัวจริง บอร์ดหนึ่งตัวอยู่ได้ที่เดียว ถ้าบอร์ดถูกอัปเฟิร์มแวร์ใหม่
    ให้ใช้ DEVICE_ID อื่น (เช่นจาก WC09 เป็น WC07) แถว WC09 จะยังถือ MAC เดิมค้างไว้
    แล้วตอนสร้าง/อัปเดตแถว WC07 ด้วย MAC เดียวกันจะชน unique index เป็น 500

    ต้อง flush การปลดก่อน เพราะ Postgres ตรวจ unique ทีละคำสั่ง ไม่ได้รอจนจบ transaction
    ถ้าปล่อยให้ SQLAlchemy เรียงลำดับเอง มันอาจยิงคำสั่งของแถวใหม่ก่อนแล้วชนอยู่ดี
    """
    holders = (
        await session.scalars(select(Reader).where(Reader.mac == mac, Reader.code != keep_code))
    ).all()
    if not holders:
        return
    for other in holders:
        print(f"mac {mac} moved from {other.code} to {keep_code}", flush=True)
        other.mac = None
    await session.flush()


async def touch_reader(
    session: AsyncSession,
    code: str | None,
    mac: str | None = None,
    ip: str | None = None,
) -> Reader | None:
    """หาเครื่องอ่านจากรหัส ถ้ายังไม่เคยเห็นก็สร้างให้เลย

    สร้างด้วย registered=False เพื่อให้ไปโผล่ในกล่อง "พบอุปกรณ์ใหม่" ของหน้าเว็บ
    รอคนกดยืนยัน ไม่ใช่โผล่ในรายการเครื่องที่ใช้งานจริงทันที

    เชื่อ code เป็นตัวระบุหลัก ไม่ใช่ MAC เพราะ code คือสิ่งที่ประวัติการเข้าออก
    อ้างถึง และเป็นสิ่งที่คนตั้งเอง ส่วน MAC เก็บไว้ยืนยันตัวฮาร์ดแวร์
    """
    if not code:
        return None

    if mac:
        await _release_mac(session, mac, keep_code=code)

    reader = await session.scalar(select(Reader).where(Reader.code == code))
    if reader is None:
        reader = Reader(code=code, mac=mac, registered=False)
        session.add(reader)
        await session.flush()
    elif mac and reader.mac != mac:
        # บอร์ดถูกเปลี่ยนตัวแต่ยังใช้รหัสเดิม (เช่นบอร์ดเดิมเสียแล้วเอาตัวใหม่มาแทน)
        # ทับค่าเดิมไปเลย ให้ MAC ตรงกับบอร์ดที่ยิงเข้ามาล่าสุดเสมอ
        reader.mac = mac

    reader.last_seen_at = datetime.now(timezone.utc)
    if ip:
        reader.last_ip = ip
    return reader


def record_health(
    reader: Reader,
    firmware: str | None,
    rc522_ok: bool | None,
    recoveries: int | None,
    uptime_s: int | None,
) -> None:
    """บันทึกสุขภาพที่บอร์ดรายงานมา และสะสมยอดการกู้ RC522 ไว้ข้ามการรีสตาร์ท

    ตัวนับในบอร์ดอยู่ใน RAM จึงกลับเป็น 0 ทุกครั้งที่บอร์ดรีสตาร์ท ซึ่งเกิดวันละครั้ง
    จากการรีสตาร์ทอัตโนมัติ ถ้าเก็บแค่ค่าล่าสุดจะดูไม่ออกว่าเครื่องไหนหลุดบ่อยในระยะยาว

    วิธีสะสม: ถ้าบอร์ดยังไม่รีสตาร์ท บวกเฉพาะส่วนที่เพิ่มขึ้นจากครั้งก่อน
    ถ้ารีสตาร์ทไปแล้ว บวกค่าที่ส่งมาทั้งหมด เพราะเป็นการนับใหม่ตั้งแต่บูต

    ตรวจการรีสตาร์ทจาก uptime ที่ลดลงเป็นหลัก ไม่ใช่จากตัวนับที่ลดลงอย่างเดียว
    เพราะถ้าบอร์ดรีสตาร์ทแล้วกู้ RC522 ได้หลายครั้งก่อนประกาศตัวรอบแรก ตัวนับอาจไม่ได้ลดลง
    เช่น ก่อนรีสตาร์ทนับได้ 2 หลังรีสตาร์ทนับได้ 3 ถ้าดูแค่ตัวนับจะบวกเพิ่มแค่ 1 ซึ่งผิด
    """
    if firmware:
        reader.firmware = firmware
    if rc522_ok is not None:
        reader.rc522_ok = rc522_ok

    if recoveries is not None:
        prev = reader.rc522_recoveries
        prev_uptime = reader.uptime_s
        rebooted = (
            prev is None
            or recoveries < prev
            or (uptime_s is not None and prev_uptime is not None and uptime_s < prev_uptime)
        )
        delta = recoveries if rebooted else recoveries - prev
        reader.rc522_recoveries_total = (reader.rc522_recoveries_total or 0) + delta
        reader.rc522_recoveries = recoveries

    if uptime_s is not None:
        reader.uptime_s = uptime_s


def client_ip(request) -> str | None:
    """IP จริงของอุปกรณ์

    ถ้าวันหลังเอา reverse proxy มาคั่น ให้เชื่อ X-Forwarded-For เป็นอันดับแรก
    ไม่งั้นทุกเครื่องจะกลายเป็น IP ของ proxy หมด
    """
    forwarded = request.headers.get("x-forwarded-for")
    if forwarded:
        # อาจมีหลายค่าคั่นด้วยจุลภาค ตัวแรกคือ client ต้นทาง
        return forwarded.split(",")[0].strip()
    return request.client.host if request.client else None
