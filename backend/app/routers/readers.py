from fastapi import APIRouter, Depends, Query, Request, status
from sqlalchemy import func, select
from sqlalchemy.ext.asyncio import AsyncSession

from ..database import get_session
from ..devices import client_ip, record_health, touch_reader
from ..errors import commit_or_conflict, guard_unique, not_found
from ..models import Reader
from ..schemas import (
    AnnounceIn,
    AnnounceOut,
    ReaderCreate,
    ReaderOut,
    ReaderPage,
    ReaderUpdate,
)

router = APIRouter(prefix="/api/readers", tags=["readers"])


@router.get("", response_model=ReaderPage)
async def list_readers(
    limit: int = Query(default=20, ge=1, le=200),
    offset: int = Query(default=0, ge=0),
    registered: bool | None = Query(
        default=True,
        description="True = เฉพาะที่ลงทะเบียนแล้ว, False = เฉพาะที่เพิ่งพบ, ไม่ส่ง = ทั้งหมด",
    ),
    session: AsyncSession = Depends(get_session),
):
    def _filtered(stmt):
        return stmt if registered is None else stmt.where(Reader.registered.is_(registered))

    total = await session.scalar(_filtered(select(func.count()).select_from(Reader)))
    rows = await session.scalars(
        _filtered(select(Reader)).order_by(Reader.code).limit(limit).offset(offset)
    )
    return ReaderPage(items=list(rows.all()), total=total or 0)


@router.post("/announce", response_model=AnnounceOut)
async def announce(payload: AnnounceIn, request: Request, session: AsyncSession = Depends(get_session)):
    """อุปกรณ์ประกาศตัวเองตอนบูตและทุก ๆ ไม่กี่นาที

    ทำให้เครื่องใหม่โผล่บนหน้าเว็บได้ทันทีที่เสียบไฟและต่อ WiFi ติด
    โดยไม่ต้องรอให้ใครเอาบัตรมาแตะก่อน
    """
    reader = await touch_reader(session, payload.device_id, payload.mac, client_ip(request))
    record_health(
        reader,
        firmware=payload.firmware,
        rc522_ok=payload.rc522_ok,
        recoveries=payload.rc522_recoveries,
        uptime_s=payload.uptime_s,
    )
    await commit_or_conflict(session)
    await session.refresh(reader)
    return AnnounceOut(
        code=reader.code,
        registered=reader.registered,
        name=reader.name,
        known_ip=reader.last_ip,
    )


@router.post("", response_model=ReaderOut, status_code=status.HTTP_201_CREATED)
async def create_reader(payload: ReaderCreate, session: AsyncSession = Depends(get_session)):
    await guard_unique(
        session,
        lambda: session.scalar(
            select(func.count())
            .select_from(Reader)
            .where(Reader.code == payload.code, Reader.registered.is_(True))
        ),
        "duplicate_reader_code",
        "A reader with this code already exists",
        {"code": payload.code},
    )

    # ถ้าอุปกรณ์เคยประกาศตัวมาแล้ว ให้เปลี่ยนแถวเดิมเป็น "ลงทะเบียนแล้ว"
    # แทนที่จะสร้างใหม่ซ้ำ ไม่งั้นจะชน unique ที่คอลัมน์ code
    reader = await session.scalar(select(Reader).where(Reader.code == payload.code))
    if reader is None:
        reader = Reader(**payload.model_dump(), registered=True)
        session.add(reader)
    else:
        for field, value in payload.model_dump().items():
            if value is not None or field in ("name", "location", "note"):
                setattr(reader, field, value)
        reader.registered = True
    # เครื่องอ่านอาจถูก auto-register จากการแตะบัตรพร้อมกันพอดี จึงต้องกัน race ที่ commit ด้วย
    await commit_or_conflict(session)
    await session.refresh(reader)
    return reader


@router.patch("/{reader_id}", response_model=ReaderOut)
async def update_reader(reader_id: int, payload: ReaderUpdate, session: AsyncSession = Depends(get_session)):
    reader = await session.get(Reader, reader_id)
    if not reader:
        raise not_found("reader_not_found", "Reader not found")

    for field, value in payload.model_dump(exclude_unset=True).items():
        setattr(reader, field, value)
    await commit_or_conflict(session)
    await session.refresh(reader)
    return reader


@router.delete("/{reader_id}", status_code=status.HTTP_204_NO_CONTENT)
async def delete_reader(reader_id: int, session: AsyncSession = Depends(get_session)):
    reader = await session.get(Reader, reader_id)
    if not reader:
        raise not_found("reader_not_found", "Reader not found")
    # ประวัติยังอยู่ครบ เพราะ reader_id ตั้งเป็น ON DELETE SET NULL
    await session.delete(reader)
    await session.commit()
