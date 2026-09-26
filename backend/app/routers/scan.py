from fastapi import APIRouter, Depends, Request
from sqlalchemy import select
from sqlalchemy.ext.asyncio import AsyncSession

from ..database import get_session
from ..devices import client_ip, touch_reader
from ..models import AccessLog, Card
from ..schemas import ScanIn, ScanOut

router = APIRouter(prefix="/api", tags=["scan"])


def _fit(text: str, width: int = 16) -> str:
    """จอ LCD กว้าง 16 ตัวอักษร ตัดตั้งแต่ฝั่งเซิร์ฟเวอร์ จะได้ไม่ต้องไปตัดบน ESP"""
    return text[:width]


@router.post("/scan", response_model=ScanOut)
async def scan(payload: ScanIn, request: Request, session: AsyncSession = Depends(get_session)):
    """ESP8266 ยิงเข้ามาทุกครั้งที่มีการแตะบัตร

    ตอบกลับสั้น ๆ พอให้เครื่องอ่านตัดสินใจว่าจะ ACCESS OK หรือ DENIED
    และบันทึกทุกครั้งลง access_logs ไม่ว่าผลจะเป็นอย่างไร
    """
    reader = await touch_reader(session, payload.device_id, payload.mac, client_ip(request))

    card = await session.scalar(select(Card).where(Card.uid == payload.uid))

    if card is None:
        allowed, reason, name = False, "unknown_card", "Unknown card"
    elif not card.active:
        allowed, reason, name = False, "card_disabled", card.holder_name
    elif reader is not None and not reader.active:
        # เครื่องถูกสั่งปิดจากหน้าเว็บ บัตรดีก็ไม่ให้ผ่าน
        allowed, reason, name = False, "reader_disabled", card.holder_name
    else:
        allowed, reason, name = True, "ok", card.holder_name

    log = AccessLog(
        uid=payload.uid,
        card_id=card.id if card else None,
        reader_id=reader.id if reader else None,
        holder_name=card.holder_name if card else None,
        barcode=card.barcode if card else None,
        # ชนิดบัตรที่เครื่องอ่านได้ ถ้าไม่ส่งมาก็ใช้ค่าที่ลงทะเบียนไว้
        card_type=payload.card_type or (card.card_type if card else None),
        reader_code=payload.device_id,
        allowed=allowed,
        reason=reason,
    )
    session.add(log)
    await session.commit()
    await session.refresh(log)

    return ScanOut(
        allowed=allowed,
        name=_fit(name),
        barcode=_fit(card.barcode) if card and card.barcode else "",
        message=_fit("ACCESS OK" if allowed else "DENIED"),
        log_id=log.id,
    )
