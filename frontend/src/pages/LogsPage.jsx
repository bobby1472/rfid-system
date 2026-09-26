import { useCallback, useEffect, useState } from 'react'
import { api } from '../api.js'
import { translateError, useLang } from '../i18n.jsx'
import Pagination from '../components/Pagination.jsx'

const PAGE_SIZE = 20

export default function LogsPage() {
  const { lang, t } = useLang()

  const [logs, setLogs] = useState([])
  const [total, setTotal] = useState(0)
  const [offset, setOffset] = useState(0)
  const [readers, setReaders] = useState([])
  const [filter, setFilter] = useState({ uid: '', reader_code: '', allowed: '' })
  const [live, setLive] = useState(true)
  const [error, setError] = useState(null)

  const activeFilters = useCallback(() => {
    const params = {}
    if (filter.uid) params.uid = filter.uid
    if (filter.reader_code) params.reader_code = filter.reader_code
    if (filter.allowed !== '') params.allowed = filter.allowed
    return params
  }, [filter])

  const load = useCallback(async () => {
    try {
      const page = await api.listLogs({ ...activeFilters(), limit: PAGE_SIZE, offset })
      setLogs(page.items)
      setTotal(page.total)
      setError(null)
    } catch (e) {
      setError(translateError(t, e))
    }
  }, [activeFilters, offset, t])

  useEffect(() => { load() }, [load])

  // ดึงเครื่องอ่านทั้งหมดมาใส่ dropdown ไม่ใช่แค่หน้าแรก
  useEffect(() => {
    api.listReaders({ limit: 200 }).then((p) => setReaders(p.items)).catch(() => {})
  }, [])

  useEffect(() => {
    if (!live) return
    const timer = setInterval(load, 3000)
    return () => clearInterval(timer)
  }, [live, load])

  // ให้เบราว์เซอร์โหลดไฟล์เอง จะได้ไม่ต้องอุ้มไฟล์ทั้งก้อนไว้ในหน่วยความจำ
  // ส่งตัวกรองชุดเดียวกับที่เห็นบนหน้าจอไปด้วย ไฟล์จะได้ตรงกับที่ดูอยู่
  const exportExcel = () => {
    window.location.href = api.exportLogsUrl(activeFilters())
  }

  // เปลี่ยนตัวกรองแล้วต้องกลับหน้าแรกเสมอ ไม่งั้นค้างอยู่หน้า 5 ของผลลัพธ์ที่มี 2 หน้า
  const set = (k) => (e) => {
    setOffset(0)
    setFilter((f) => ({ ...f, [k]: e.target.value }))
  }

  const locale = lang === 'th' ? 'th-TH' : 'en-GB'

  return (
    <>
      {error && <div className="msg error">{error}</div>}

      <div className="panel">
        <h2>{t('log.title', { n: total })}</h2>

        <div className="row" style={{ marginBottom: 14 }}>
          <div style={{ flex: '1 1 160px' }}>
            <label>{t('log.uid')}</label>
            <input className="mono" value={filter.uid} onChange={set('uid')} placeholder={t('log.all')} />
          </div>
          <div style={{ flex: '1 1 140px' }}>
            <label>{t('log.reader')}</label>
            <select value={filter.reader_code} onChange={set('reader_code')}>
              <option value="">{t('log.all')}</option>
              {readers.map((r) => (
                <option key={r.id} value={r.code}>{r.code}{r.name ? ` - ${r.name}` : ''}</option>
              ))}
            </select>
          </div>
          <div style={{ flex: '1 1 120px' }}>
            <label>{t('log.result')}</label>
            <select value={filter.allowed} onChange={set('allowed')}>
              <option value="">{t('log.all')}</option>
              <option value="true">{t('log.allowed')}</option>
              <option value="false">{t('log.denied')}</option>
            </select>
          </div>
          <button className="ghost" onClick={load}>{t('log.refresh')}</button>
          <button className="ghost" onClick={exportExcel} disabled={total === 0}>{t('log.export')}</button>
          <label style={{ display: 'flex', gap: 7, alignItems: 'center', color: 'var(--text)', marginBottom: 9 }}>
            <input type="checkbox" checked={live} onChange={(e) => setLive(e.target.checked)} style={{ width: 16 }} />
            {t('log.live')}
          </label>
        </div>

        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>{t('log.time')}</th><th>{t('log.reader')}</th><th>{t('log.uid')}</th>
                <th>{t('log.barcode')}</th><th>{t('log.holder')}</th><th>{t('log.type')}</th>
                <th>{t('log.result')}</th><th>{t('log.reason')}</th>
              </tr>
            </thead>
            <tbody>
              {logs.map((l) => (
                <tr key={l.id}>
                  <td style={{ whiteSpace: 'nowrap' }}>
                    {new Date(l.scanned_at).toLocaleString(locale, { dateStyle: 'short', timeStyle: 'medium' })}
                  </td>
                  <td className="mono">{l.reader_code || '-'}</td>
                  <td className="mono">{l.uid}</td>
                  <td className="mono">{l.barcode || '-'}</td>
                  <td>
                    {l.holder_name || <span style={{ color: 'var(--muted)' }}>{t('log.unknownHolder')}</span>}
                  </td>
                  <td>{l.card_type || '-'}</td>
                  <td>
                    <span className={'badge ' + (l.allowed ? 'ok' : 'deny')}>
                      {l.allowed ? t('log.allowed') : t('log.denied')}
                    </span>
                  </td>
                  <td style={{ color: 'var(--muted)' }}>{t(`reason.${l.reason}`)}</td>
                </tr>
              ))}
            </tbody>
          </table>
          {logs.length === 0 && <div className="empty">{t('log.empty')}</div>}
        </div>

        <Pagination total={total} limit={PAGE_SIZE} offset={offset} onChange={setOffset} />
      </div>
    </>
  )
}
