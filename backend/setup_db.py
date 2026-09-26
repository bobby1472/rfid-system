"""สร้างฐานข้อมูลและตารางให้พร้อมใช้งาน

รันครั้งเดียวก่อนเริ่มเซิร์ฟเวอร์:
    python setup_db.py
"""
import asyncio
import sys

from sqlalchemy import text
from sqlalchemy.ext.asyncio import create_async_engine

from app.config import get_settings
from app.database import Base
from app.migrate import run as run_migrations
from app import models  # noqa: F401  - ต้อง import เพื่อให้ metadata รู้จักตาราง


async def main() -> int:
    settings = get_settings()

    if not settings.postgres_password:
        print("!! ยังไม่ได้ตั้ง POSTGRES_PASSWORD ในไฟล์ .env")
        print("   คัดลอก .env.example เป็น .env แล้วใส่รหัสผ่านก่อน")
        return 1

    # ขั้นที่ 1: ต่อเข้า db กลางเพื่อสร้าง "rfid-system"
    admin = create_async_engine(settings.admin_database_url, isolation_level="AUTOCOMMIT")
    try:
        async with admin.connect() as conn:
            exists = await conn.scalar(
                text("SELECT 1 FROM pg_database WHERE datname = :name"),
                {"name": settings.postgres_db},
            )
            if exists:
                print(f"-  ฐานข้อมูล \"{settings.postgres_db}\" มีอยู่แล้ว")
            else:
                # ชื่อมีขีดกลางจึงต้องครอบด้วยอัญประกาศคู่
                await conn.execute(text(f'CREATE DATABASE "{settings.postgres_db}"'))
                print(f"+  สร้างฐานข้อมูล \"{settings.postgres_db}\" แล้ว")
    except Exception as exc:
        print(f"!! ต่อ PostgreSQL ไม่ได้: {exc}")
        print("   ตรวจ host/port/user/password ในไฟล์ .env")
        return 1
    finally:
        # dispose() เองก็โยน error ได้ (เช่นตอนที่ greenlet ยังไม่ถูกติดตั้ง)
        # ถ้าไม่ดัก traceback ของมันจะกลบข้อความที่เราเพิ่งพิมพ์บอกผู้ใช้ไป
        try:
            await admin.dispose()
        except Exception:
            pass

    # ขั้นที่ 2: สร้างตารางในฐานข้อมูลนั้น
    app_engine = create_async_engine(settings.database_url)
    try:
        async with app_engine.begin() as conn:
            await conn.run_sync(Base.metadata.create_all)
            applied = await run_migrations(conn)
        print("+  สร้างตาราง cards, readers และ access_logs แล้ว")
        for item in applied:
            print(f"+  เพิ่มคอลัมน์ {item}")
    finally:
        await app_engine.dispose()

    print("\nเรียบร้อย เริ่มเซิร์ฟเวอร์ได้ด้วย:")
    print("   uvicorn app.main:app --host 0.0.0.0 --port 7777 --reload")
    return 0


if __name__ == "__main__":
    sys.exit(asyncio.run(main()))
