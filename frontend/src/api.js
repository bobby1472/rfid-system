/*
 * ที่อยู่ของ API หาเองตอนรัน ไม่ฝังไว้ตอน build
 *
 * ตอน dev: ใช้ path สัมพัทธ์ เพราะ vite ทำ proxy /api ให้อยู่แล้ว (ดู vite.config.js)
 * ตอน production: ประกอบ URL จาก hostname ของหน้าเว็บเอง + พอร์ตของ API
 *
 * ทำแบบนี้เพราะไม่ได้ใช้ reverse proxy ของ IIS (ไม่ได้ลง ARR) เบราว์เซอร์จึงต้อง
 * ยิงหา FastAPI ตรง ๆ ข้าม origin — และการอ่าน hostname ตอนรันทำให้ไฟล์ที่ build
 * แล้วชุดเดียว เปิดจากเครื่องไหนก็ยิงถูกเครื่องนั้น ไม่ต้อง build ใหม่เวลา IP เปลี่ยน
 *
 * ถ้าอยากล็อกที่อยู่ตายตัว ตั้ง VITE_API_BASE ในไฟล์ .env.production ได้
 */
const API_PORT = import.meta.env.VITE_API_PORT || '7777'

const BASE =
  import.meta.env.VITE_API_BASE ||
  (import.meta.env.DEV
    ? '/api'
    : `${window.location.protocol}//${window.location.hostname}:${API_PORT}/api`)

/** ข้อผิดพลาดจาก API ที่พก code/params มาด้วย เพื่อให้หน้าเว็บแปลเป็นภาษาที่เลือกได้ */
export class ApiError extends Error {
  constructor({ code, message, params, status }) {
    super(message || `HTTP ${status}`)
    this.name = 'ApiError'
    this.code = code
    this.params = params
    this.status = status
  }
}

async function request(path, options = {}) {
  const res = await fetch(BASE + path, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  })

  if (res.status === 204) return null

  const body = await res.json().catch(() => null)
  if (!res.ok) {
    const detail = body?.detail

    // รูปแบบหลัก: backend ส่ง {code, message, params} มาให้แปลเอง
    if (detail && typeof detail === 'object' && !Array.isArray(detail)) {
      throw new ApiError({ ...detail, status: res.status })
    }
    // validation error ของ FastAPI มาเป็น array ของ object
    if (Array.isArray(detail)) {
      throw new ApiError({ message: detail.map((d) => d.msg).join(', '), status: res.status })
    }
    throw new ApiError({ message: typeof detail === 'string' ? detail : null, status: res.status })
  }
  return body
}

// ตัดค่าว่างทิ้งก่อนส่ง ไม่งั้น ?uid= จะกลายเป็นตัวกรองที่ไม่มีทางตรงกับอะไร
function qs(params) {
  const pairs = Object.entries(params).filter(([, v]) => v !== '' && v !== undefined && v !== null)
  const s = new URLSearchParams(pairs).toString()
  return s ? `?${s}` : ''
}

export const api = {
  stats: () => request('/stats'),

  listCards: (params = {}) => request(`/cards${qs(params)}`),
  createCard: (data) => request('/cards', { method: 'POST', body: JSON.stringify(data) }),
  updateCard: (id, data) => request(`/cards/${id}`, { method: 'PATCH', body: JSON.stringify(data) }),
  deleteCard: (id) => request(`/cards/${id}`, { method: 'DELETE' }),

  listReaders: (params = {}) => request(`/readers${qs(params)}`),
  createReader: (data) => request('/readers', { method: 'POST', body: JSON.stringify(data) }),
  updateReader: (id, data) => request(`/readers/${id}`, { method: 'PATCH', body: JSON.stringify(data) }),
  deleteReader: (id) => request(`/readers/${id}`, { method: 'DELETE' }),

  listLogs: (params = {}) => request(`/logs${qs(params)}`),
  latestLog: () => request('/logs/latest'),
  exportLogsUrl: (params = {}) => `${BASE}/logs/export${qs(params)}`,
  baseUrl: () => BASE,
}
