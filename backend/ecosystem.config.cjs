/*
 * pm2 config สำหรับ backend ที่เป็น Python (FastAPI + uvicorn)
 *
 * pm2 รันอะไรก็ได้ที่เป็น executable ไม่จำเป็นต้องเป็น Node
 * เคล็ดลับคือชี้ script ไปที่ python.exe แล้วส่ง -m uvicorn ผ่าน args
 *
 * ใช้นามสกุล .cjs เพราะ pm2 อ่าน config ด้วย require() ซึ่งเป็น CommonJS
 *
 * คำสั่งที่ใช้บ่อย (รันจากโฟลเดอร์ backend):
 *   pm2 start ecosystem.config.cjs      เริ่มทำงาน
 *   pm2 logs rfid-api                   ดู log สด
 *   pm2 restart rfid-api                รีสตาร์ทหลังแก้โค้ด
 *   pm2 stop rfid-api                   หยุด
 *   pm2 delete rfid-api                 เอาออกจากรายการ pm2
 *   pm2 save                            จำรายการไว้ใช้ตอน resurrect
 */
const path = require('path')

// อ้างอิงจากที่อยู่ของไฟล์นี้ ย้ายโปรเจกต์ไปไดรฟ์อื่นก็ยังใช้ได้
const BACKEND_DIR = __dirname
// ใช้ pythonw.exe ไม่ใช่ python.exe
// pythonw ถูกคอมไพล์เป็นโปรแกรมแบบ GUI subsystem ทำให้ Windows ไม่สร้าง console
// ให้ ไม่งั้นจะมีหน้าต่างดำเด้งขึ้นมาทุกครั้งที่ pm2 สตาร์ท
// (ลอง windowsHide: true ของ pm2 แล้วไม่ได้ผล เพราะ pm2 ไม่ได้ส่งค่านี้ต่อให้ spawn)
// stdout/stderr ยังเขียน log ได้ปกติ เพราะ pm2 ต่อ pipe ให้อยู่แล้ว
const PYTHON = path.join(BACKEND_DIR, '.venv', 'Scripts', 'pythonw.exe')

module.exports = {
  apps: [
    {
      name: 'rfid-api',

      // เรียก python.exe ของ venv ตรง ๆ ไม่ใช่ python ของระบบ
      // ไม่งั้นจะหา fastapi/asyncpg ไม่เจอ เพราะแพ็กเกจติดตั้งอยู่ใน venv
      script: PYTHON,
      args: ['-m', 'uvicorn', 'app.main:app', '--host', '0.0.0.0', '--port', '7777'],

      // สำคัญที่สุด: pydantic-settings อ่าน .env แบบ path สัมพัทธ์ (env_file=".env")
      // ถ้า cwd ไม่ใช่โฟลเดอร์นี้ จะหารหัสผ่าน PostgreSQL ไม่เจอแล้วสตาร์ทไม่ขึ้น
      cwd: BACKEND_DIR,

      // บอก pm2 ว่าอย่าเอา node มาครอบ ให้รัน executable ตรง ๆ
      interpreter: 'none',

      // cluster mode เป็นของ Node เท่านั้น Python ต้องใช้ fork
      exec_mode: 'fork',
      instances: 1,

      env: {
        // ไม่พักข้อมูลใน buffer log จะได้โผล่ใน pm2 logs ทันที ไม่ใช่รอเป็นนาที
        PYTHONUNBUFFERED: '1',
        // กัน UnicodeEncodeError ตอนพิมพ์ภาษาไทยลง log เพราะ Windows ใช้ cp1252 เป็นค่าเริ่มต้น
        PYTHONIOENCODING: 'utf-8',
      },

      autorestart: true,
      // ถ้าพังเร็วกว่านี้ถือว่าสตาร์ทไม่สำเร็จจริง ไม่ใช่แค่สะดุด
      min_uptime: '10s',
      // ครบแล้วให้ขึ้นสถานะ errored ไปเลย ดีกว่าวนรีสตาร์ทไม่รู้จบจนกิน CPU
      max_restarts: 10,
      // หน่วงเพิ่มขึ้นเรื่อย ๆ เผื่อกรณีที่ PostgreSQL ยังไม่พร้อมตอนบูตเครื่อง
      exp_backoff_restart_delay: 2000,

      // ให้เวลา uvicorn ปิด connection ที่ค้างอยู่ก่อนถูกฆ่า
      kill_timeout: 5000,

      // ห้ามเปิด watch: ตัว pm2 จะเห็นไฟล์ log ที่ตัวเองเขียนแล้วรีสตาร์ทวนไม่จบ
      // จะรีโหลดหลังแก้โค้ด ใช้ pm2 restart rfid-api เอา
      watch: false,

      out_file: path.join(BACKEND_DIR, 'logs', 'rfid-api-out.log'),
      error_file: path.join(BACKEND_DIR, 'logs', 'rfid-api-error.log'),
      merge_logs: true,
      time: true,
    },
  ],
}
