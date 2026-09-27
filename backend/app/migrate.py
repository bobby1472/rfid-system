"""เพิ่มคอลัมน์ใหม่ให้ตารางที่มีข้อมูลอยู่แล้ว

Base.metadata.create_all สร้างได้แค่ตารางที่ยังไม่มี มันไม่แก้ตารางเดิม
พอเพิ่มคอลัมน์ในโมเดล ฐานข้อมูลที่ใช้งานอยู่จะยังไม่มีคอลัมน์นั้น
แล้วแอปจะพังด้วย UndefinedColumnError ทันทีที่ query

โปรเจกต์นี้ยังไม่ใหญ่พอจะคุ้มกับการลง Alembic จึงใช้ ALTER แบบ idempotent
เรียกได้ซ้ำกี่รอบก็ได้ ทั้งตอนรัน setup_db.py และตอนแอปบูต
"""
from sqlalchemy import text
from sqlalchemy.ext.asyncio import AsyncConnection

# (ตาราง, คอลัมน์, นิยามชนิดข้อมูล)
_COLUMNS = [
    ("readers", "mac", "VARCHAR(17)"),
    ("readers", "registered", "BOOLEAN NOT NULL DEFAULT false"),
    # สุขภาพของเครื่อง - 04_migrate_reader_health.sql
    ("readers", "firmware", "VARCHAR(32)"),
    ("readers", "rc522_ok", "BOOLEAN"),
    ("readers", "rc522_recoveries", "INTEGER"),
    ("readers", "rc522_recoveries_total", "INTEGER NOT NULL DEFAULT 0"),
    ("readers", "uptime_s", "INTEGER"),
]

# ดัชนีที่ต้องมีคู่กับคอลัมน์ใหม่
_INDEXES = [
    ("ix_readers_mac", "CREATE UNIQUE INDEX IF NOT EXISTS ix_readers_mac ON readers (mac)"),
]


async def run(conn: AsyncConnection) -> list[str]:
    """คืนรายการสิ่งที่เปลี่ยนจริง ถ้าไม่มีอะไรต้องทำจะคืนลิสต์ว่าง"""
    applied: list[str] = []

    for table, column, ddl in _COLUMNS:
        exists = await conn.scalar(
            text(
                "SELECT 1 FROM information_schema.columns "
                "WHERE table_name = :t AND column_name = :c"
            ),
            {"t": table, "c": column},
        )
        if exists:
            continue
        await conn.execute(text(f"ALTER TABLE {table} ADD COLUMN {column} {ddl}"))
        applied.append(f"{table}.{column}")

        # เครื่องที่มีอยู่ก่อนหน้านี้ถูกตั้งชื่อและใช้งานจริงมาแล้ว
        # จึงถือว่าลงทะเบียนเรียบร้อย ไม่ควรไปโผล่ในกล่อง "พบอุปกรณ์ใหม่"
        if (table, column) == ("readers", "registered"):
            await conn.execute(text("UPDATE readers SET registered = true"))
            applied.append("readers.registered = true (แถวเดิม)")

    for name, ddl in _INDEXES:
        await conn.execute(text(ddl))

    return applied
