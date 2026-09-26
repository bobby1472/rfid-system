import socket
from contextlib import asynccontextmanager

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .config import get_settings
from .database import Base, engine
from .migrate import run as run_migrations
from .routers import cards, logs, readers, scan

settings = get_settings()


@asynccontextmanager
async def lifespan(app: FastAPI):
    # สร้างตารางอัตโนมัติถ้ายังไม่มี (ฐานข้อมูลต้องถูกสร้างไว้ก่อนด้วย setup_db.py)
    async with engine.begin() as conn:
        await conn.run_sync(Base.metadata.create_all)
        # create_all ไม่แก้ตารางเดิม ต้องเติมคอลัมน์ใหม่เองที่นี่
        # ไม่งั้น pm2 restart หลังอัปเดตโค้ดแล้วแอปจะพังทันที
        applied = await run_migrations(conn)
        for item in applied:
            print(f"migrated: {item}", flush=True)
    yield
    await engine.dispose()


app = FastAPI(title="RFID System API", version="1.0.0", lifespan=lifespan)

app.add_middleware(
    CORSMiddleware,
    allow_origins=settings.cors_list,
    allow_origin_regex=settings.cors_origin_regex or None,
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

app.include_router(cards.router)
app.include_router(readers.router)
app.include_router(scan.router)
app.include_router(logs.router)


@app.get("/api/health")
async def health():
    """ESP8266 ใช้เช็คว่าเซิร์ฟเวอร์ยังอยู่ไหม และคนใช้ดู IP ที่ต้องกรอกลงเฟิร์มแวร์"""
    return {"status": "ok", "hostname": socket.gethostname()}
