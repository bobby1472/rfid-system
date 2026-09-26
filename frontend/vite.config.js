import { defineConfig } from 'vite'
import react from '@vitejs/plugin-react'

export default defineConfig({
  plugins: [react()],
  server: {
    port: 5173,
    // เปิดให้เครื่องอื่นในวง LAN เข้าหน้าเว็บได้ด้วย
    host: true,
    proxy: {
      // เรียก /api ผ่าน dev server ไปที่ FastAPI จะได้ไม่ต้องยุ่งกับ CORS ตอนพัฒนา
      '/api': { target: 'http://localhost:7777', changeOrigin: true },
    },
  },
})
