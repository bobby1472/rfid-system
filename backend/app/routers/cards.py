from fastapi import APIRouter, Depends, Query, status
from sqlalchemy import Select, func, or_, select
from sqlalchemy.ext.asyncio import AsyncSession

from ..database import get_session
from ..errors import commit_or_conflict, guard_unique, not_found
from ..models import Card
from ..schemas import CardCreate, CardOut, CardPage, CardUpdate

router = APIRouter(prefix="/api/cards", tags=["cards"])


async def _uid_taken(session: AsyncSession, uid: str) -> bool:
    return bool(await session.scalar(select(func.count()).select_from(Card).where(Card.uid == uid)))


async def _barcode_taken(session: AsyncSession, barcode: str, exclude_id: int | None = None) -> bool:
    stmt = select(func.count()).select_from(Card).where(Card.barcode == barcode)
    if exclude_id is not None:
        stmt = stmt.where(Card.id != exclude_id)
    return bool(await session.scalar(stmt))


@router.get("", response_model=CardPage)
async def list_cards(
    q: str | None = Query(default=None, description="ค้นหาจาก UID, ชื่อผู้ถือ หรือบาร์โค้ด"),
    active: bool | None = None,
    limit: int = Query(default=20, ge=1, le=200),
    offset: int = Query(default=0, ge=0),
    session: AsyncSession = Depends(get_session),
):
    def _filtered(stmt: Select) -> Select:
        s = stmt
        if q:
            like = f"%{q.strip()}%"
            s = s.where(or_(Card.uid.ilike(like), Card.holder_name.ilike(like), Card.barcode.ilike(like)))
        if active is not None:
            s = s.where(Card.active.is_(active))
        return s

    # total นับตามตัวกรองเดียวกัน ไม่ใช่ทั้งตาราง ไม่งั้นจำนวนหน้าจะผิดตอนค้นหา
    total = await session.scalar(_filtered(select(func.count()).select_from(Card)))
    rows = await session.scalars(
        _filtered(select(Card)).order_by(Card.created_at.desc()).limit(limit).offset(offset)
    )
    return CardPage(items=list(rows.all()), total=total or 0)


@router.post("", response_model=CardOut, status_code=status.HTTP_201_CREATED)
async def create_card(payload: CardCreate, session: AsyncSession = Depends(get_session)):
    await guard_unique(
        session,
        lambda: _uid_taken(session, payload.uid),
        "duplicate_uid",
        "This UID is already registered",
        {"uid": payload.uid},
    )
    # บาร์โค้ดก็เป็น unique เหมือนกัน ถ้าไม่ตรวจจะหลุดเป็น 500 ตอน commit
    if payload.barcode:
        await guard_unique(
            session,
            lambda: _barcode_taken(session, payload.barcode),
            "duplicate_barcode",
            "This barcode is already used by another card",
            {"barcode": payload.barcode},
        )

    card = Card(**payload.model_dump())
    session.add(card)
    await commit_or_conflict(session)
    await session.refresh(card)
    return card


@router.get("/{card_id}", response_model=CardOut)
async def get_card(card_id: int, session: AsyncSession = Depends(get_session)):
    card = await session.get(Card, card_id)
    if not card:
        raise not_found("card_not_found", "Card not found")
    return card


@router.patch("/{card_id}", response_model=CardOut)
async def update_card(card_id: int, payload: CardUpdate, session: AsyncSession = Depends(get_session)):
    card = await session.get(Card, card_id)
    if not card:
        raise not_found("card_not_found", "Card not found")

    changes = payload.model_dump(exclude_unset=True)

    # แก้บาร์โค้ดไปทับใบอื่นก็ชน unique index ได้เหมือนกัน
    new_barcode = changes.get("barcode")
    if new_barcode:
        await guard_unique(
            session,
            lambda: _barcode_taken(session, new_barcode, exclude_id=card_id),
            "duplicate_barcode",
            "This barcode is already used by another card",
            {"barcode": new_barcode},
        )

    for field, value in changes.items():
        setattr(card, field, value)
    await commit_or_conflict(session)
    await session.refresh(card)
    return card


@router.delete("/{card_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_card(card_id: int, session: AsyncSession = Depends(get_session)):
    card = await session.get(Card, card_id)
    if not card:
        raise not_found("card_not_found", "Card not found")
    # ประวัติการเข้าออกยังอยู่ครบ เพราะ card_id ตั้งเป็น ON DELETE SET NULL
    await session.delete(card)
    await session.commit()
