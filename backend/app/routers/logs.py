from datetime import datetime, time, timedelta
from zoneinfo import ZoneInfo

from fastapi import APIRouter, Depends, Query
from fastapi.responses import StreamingResponse
from sqlalchemy import Select, false, func, select
from sqlalchemy.ext.asyncio import AsyncSession

from ..config import get_settings
from ..database import get_session
from ..excel import build_workbook, filename
from ..models import AccessLog, Card, Reader
from ..schemas import LogOut, LogPage, Stats, lenient_uid

# กันไฟล์ใหญ่จนเครื่องอืด ถ้าเกินนี้จะตัดและบอกไว้ในหัวข้อการตอบกลับ
EXPORT_MAX_ROWS = 50_000

settings = get_settings()


def _local_zone() -> ZoneInfo:
    try:
        return ZoneInfo(settings.app_timezone)
    except Exception:
        # ถ้าไม่มีฐานข้อมูลเขตเวลาในเครื่อง อย่าให้ทั้ง endpoint ล่ม
        return ZoneInfo("UTC")


def _today_start() -> datetime:
    """เที่ยงคืนของ "วันนี้" ตามเวลาท้องถิ่น ไม่ใช่ UTC

    ถ้าใช้เที่ยงคืน UTC ที่ไทย (UTC+7) วันใหม่จะเริ่มนับตอน 7 โมงเช้า
    ทำให้คนที่แตะบัตรกะเช้าหายไปจากสถิติของวันนั้นทั้งหมด
    """
    now_local = datetime.now(_local_zone())
    return datetime.combine(now_local.date(), time.min, tzinfo=now_local.tzinfo)


router = APIRouter(prefix="/api", tags=["logs"])


def _make_filter(uid: str | None, reader_code: str | None, allowed: bool | None):
    """สร้างตัวกรองชุดเดียวใช้ร่วมกันทั้งการแสดงผลและการส่งออก Excel

    ถ้าแยกกันเขียน ไฟล์ที่ส่งออกจะไม่ตรงกับที่เห็นบนหน้าจอเมื่อกฎเปลี่ยน
    """
    # ใช้กฎเดียวกับตอนบันทึก ไม่งั้นค้นด้วย "F3-27-29-07" จะไม่เจออะไรเลย
    uid_given = bool(uid and uid.strip())
    wanted_uid = lenient_uid(uid) if uid_given else None

    def _filtered(stmt: Select) -> Select:
        s = stmt
        if uid_given:
            # ส่งตัวกรองมาแต่แปลงเป็น UID ไม่ได้ (พิมพ์ผิด) -> ผลลัพธ์ต้องเป็นชุดว่าง
            s = s.where(AccessLog.uid == wanted_uid) if wanted_uid else s.where(false())
        if reader_code:
            s = s.where(AccessLog.reader_code == reader_code.strip().upper())
        if allowed is not None:
            s = s.where(AccessLog.allowed.is_(allowed))
        return s

    return _filtered


@router.get("/logs", response_model=LogPage)
async def list_logs(
    limit: int = Query(default=50, le=500),
    offset: int = 0,
    uid: str | None = None,
    reader_code: str | None = None,
    allowed: bool | None = None,
    session: AsyncSession = Depends(get_session),
):
    _filtered = _make_filter(uid, reader_code, allowed)

    total = await session.scalar(_filtered(select(func.count()).select_from(AccessLog)))
    rows = await session.scalars(
        _filtered(select(AccessLog)).order_by(AccessLog.scanned_at.desc()).limit(limit).offset(offset)
    )
    return LogPage(items=list(rows.all()), total=total or 0)


@router.get("/logs/export")
async def export_logs(
    uid: str | None = None,
    reader_code: str | None = None,
    allowed: bool | None = None,
    session: AsyncSession = Depends(get_session),
):
    """ส่งออกประวัติเป็นไฟล์ Excel ตามตัวกรองเดียวกับที่เห็นบนหน้าจอ

    ส่งออกทุกแถวที่ตรงเงื่อนไข ไม่ใช่แค่หน้าที่กำลังดูอยู่
    """
    _filtered = _make_filter(uid, reader_code, allowed)

    rows = list(
        (
            await session.scalars(
                _filtered(select(AccessLog))
                .order_by(AccessLog.scanned_at.desc())
                .limit(EXPORT_MAX_ROWS)
            )
        ).all()
    )

    buffer = build_workbook(rows, settings.app_timezone)
    name = filename(settings.app_timezone)
    return StreamingResponse(
        buffer,
        media_type="application/vnd.openxmlformats-officedocument.spreadsheetml.sheet",
        headers={
            "Content-Disposition": f'attachment; filename="{name}"',
            # ให้เบราว์เซอร์อ่านหัวนี้ได้ เผื่ออยากโชว์ว่าโดนตัดที่เพดานหรือเปล่า
            "X-Row-Count": str(len(rows)),
            "Access-Control-Expose-Headers": "Content-Disposition, X-Row-Count",
        },
    )


@router.get("/logs/latest", response_model=LogOut | None)
async def latest_log(session: AsyncSession = Depends(get_session)):
    """หน้าเว็บใช้ดึง UID ของบัตรที่เพิ่งแตะ มากรอกฟอร์มลงทะเบียนให้อัตโนมัติ"""
    return await session.scalar(select(AccessLog).order_by(AccessLog.scanned_at.desc()).limit(1))


@router.get("/stats", response_model=Stats)
async def stats(session: AsyncSession = Depends(get_session)):
    since = _today_start()
    until = since + timedelta(days=1)

    async def count(model, *where):
        return await session.scalar(select(func.count()).select_from(model).where(*where)) or 0

    in_today = (AccessLog.scanned_at >= since, AccessLog.scanned_at < until)

    return Stats(
        total_cards=await count(Card),
        active_cards=await count(Card, Card.active.is_(True)),
        total_readers=await count(Reader),
        scans_today=await count(AccessLog, *in_today),
        denied_today=await count(AccessLog, *in_today, AccessLog.allowed.is_(False)),
    )
