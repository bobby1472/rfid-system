"""อ่านค่าตั้งค่าทั้งหมดจากไฟล์ .env - ไม่มีรหัสผ่านฝังในโค้ด"""
from functools import lru_cache
from urllib.parse import quote_plus

from pydantic_settings import BaseSettings, SettingsConfigDict


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", env_file_encoding="utf-8", extra="ignore")

    postgres_host: str = "localhost"
    postgres_port: int = 5432
    postgres_user: str = "postgres"
    postgres_password: str = ""
    postgres_db: str = "rfid-system"

    api_host: str = "0.0.0.0"
    api_port: int = 7777
    cors_origins: str = "http://localhost:5173"
    # หน้าเว็บถูกเสิร์ฟจาก IIS คนละ origin กับ API จึงต้องอนุญาตข้าม origin
    # ใช้ regex เพราะเปิดจากเครื่องไหนในวง LAN ก็ได้ ระบุเป็นรายตัวไม่ไหว
    # ครอบแค่ localhost กับวง 192.168.x.x ไม่ได้เปิดให้อินเทอร์เน็ตภายนอก
    cors_origin_regex: str = r"http://(localhost|127\.0\.0\.1|192\.168\.\d{1,3}\.\d{1,3})(:\d+)?"

    # เขตเวลาที่ใช้ตัดสินว่า "วันนี้" เริ่มกี่โมง - ต้องเป็นเวลาท้องถิ่น ไม่ใช่ UTC
    # ไม่งั้นสถิติรายวันจะเริ่มนับตอน 7 โมงเช้าของไทย และกะเช้าจะหายไปจากรายงาน
    app_timezone: str = "Asia/Bangkok"

    def _dsn(self, database: str) -> str:
        # รหัสผ่านต้อง escape เผื่อมีอักขระพิเศษอย่าง @ : / #
        pwd = quote_plus(self.postgres_password)
        return (
            f"postgresql+asyncpg://{self.postgres_user}:{pwd}"
            f"@{self.postgres_host}:{self.postgres_port}/{database}"
        )

    @property
    def database_url(self) -> str:
        return self._dsn(self.postgres_db)

    @property
    def admin_database_url(self) -> str:
        """ต่อเข้า db กลางเพื่อสร้างฐานข้อมูลของเราตอน setup"""
        return self._dsn("postgres")

    @property
    def cors_list(self) -> list[str]:
        return [o.strip() for o in self.cors_origins.split(",") if o.strip()]


@lru_cache
def get_settings() -> Settings:
    return Settings()
