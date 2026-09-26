"""แปลงการชนกันของ unique constraint ให้เป็น HTTP 409 ที่อ่านรู้เรื่อง

ถ้าไม่ดักไว้ การใส่บาร์โค้ดซ้ำจะหลุดเป็น IntegrityError แล้วกลายเป็น 500
พร้อม traceback ของ Postgres ซึ่งหน้าเว็บอ่านไม่ออกและผู้ใช้ไม่รู้ว่าผิดที่ช่องไหน

detail ส่งเป็น object ที่มี code กับ params เพื่อให้หน้าเว็บแปลเป็นภาษาที่ผู้ใช้เลือกเองได้
ส่วน message เป็นข้อความอังกฤษสำรองไว้ให้เครื่องมืออย่าง curl หรือ /docs อ่าน
"""
from collections.abc import Awaitable, Callable
from typing import Any, TypeVar

from fastapi import HTTPException, status
from sqlalchemy.exc import IntegrityError
from sqlalchemy.ext.asyncio import AsyncSession

T = TypeVar("T")

# ชื่อคอลัมน์/ดัชนีที่อาจโผล่ในข้อความของ Postgres -> รหัสข้อผิดพลาดของเรา
_CONSTRAINT_CODES = {
    "barcode": ("duplicate_barcode", "This barcode is already used by another card"),
    "uid": ("duplicate_uid", "This UID is already registered"),
    "code": ("duplicate_reader_code", "A reader with this code already exists"),
    "mac": ("duplicate_mac", "This MAC address belongs to another reader"),
}


def error_detail(code: str, message: str, params: dict[str, Any] | None = None) -> dict[str, Any]:
    return {"code": code, "message": message, "params": params or {}}


# params รับเป็น dict ไม่ใช่ **kwargs เพราะคีย์ที่ใช้จริงมีทั้ง code/uid/barcode
# ซึ่งอาจไปชนกับชื่อพารามิเตอร์ของฟังก์ชันเอง
def conflict(code: str, message: str, params: dict[str, Any] | None = None) -> HTTPException:
    return HTTPException(status.HTTP_409_CONFLICT, error_detail(code, message, params))


def not_found(code: str, message: str) -> HTTPException:
    return HTTPException(status.HTTP_404_NOT_FOUND, error_detail(code, message))


async def commit_or_conflict(session: AsyncSession) -> None:
    """commit แล้วแปลง unique violation เป็น 409

    ต้อง rollback ก่อนโยน HTTPException ไม่งั้น session ค้างอยู่ในสถานะ failed
    และคำสั่งถัดไปบน session เดิมจะพังตามไปด้วย
    """
    try:
        await session.commit()
    except IntegrityError as exc:
        await session.rollback()
        text = str(getattr(exc, "orig", exc)).lower()
        for key, (code, message) in _CONSTRAINT_CODES.items():
            if key in text:
                raise conflict(code, message) from exc
        raise conflict("conflict", "This record conflicts with an existing one") from exc


async def guard_unique(
    session: AsyncSession,
    check: Callable[[], Awaitable[T]],
    code: str,
    message: str,
    params: dict[str, Any] | None = None,
) -> None:
    """ตรวจก่อนว่ามีของซ้ำอยู่แล้วไหม เพื่อให้ข้อความผิดพลาดชี้ชัดกว่า

    ยังต้องใช้ commit_or_conflict คู่กันอยู่ เพราะการตรวจก่อนแล้วค่อยเขียน
    มีช่องว่างให้เกิด race ได้ถ้ามีคนกดพร้อมกันสองคน
    """
    if await check():
        raise conflict(code, message, params)
