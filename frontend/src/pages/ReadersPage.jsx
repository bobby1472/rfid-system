import { useEffect, useRef, useState } from 'react'
import { api } from '../api.js'
import { translateError, useLang } from '../i18n.jsx'
import Pagination from '../components/Pagination.jsx'

const EMPTY = { code: '', mac: '', name: '', location: '', note: '', active: true }
const PAGE_SIZE = 20
// บอร์ดประกาศตัวทุก 5 นาที (ANNOUNCE_INTERVAL_MS) ถ้าเงียบเกินสองรอบแปลว่าหลุดไปแล้ว
// สถานะ RC522 ที่เห็นเป็นค่าเก่าค้างอยู่ ห้ามโชว์เป็นสีเขียวให้เข้าใจผิดว่ายังปกติ
const STALE_SECS = 11 * 60

const secondsSince = (iso) => (iso ? (Date.now() - new Date(iso).getTime()) / 1000 : Infinity)

export default function ReadersPage({ onChanged }) {
  const { t } = useLang()

  const [readers, setReaders] = useState([])
  const [total, setTotal] = useState(0)
  const [offset, setOffset] = useState(0)
  // การ refresh อัตโนมัติต้องอ่าน offset ล่าสุด ไม่ใช่ค่าที่ค้างอยู่ใน closure
  const offsetRef = useRef(0)

  // อุปกรณ์ที่ประกาศตัวเข้ามาแล้วแต่ยังไม่ได้ลงทะเบียน
  const [discovered, setDiscovered] = useState([])

  const [form, setForm] = useState(EMPTY)
  const [editingId, setEditingId] = useState(null)
  const [msg, setMsg] = useState(null)
  // กันกดปุ่มซ้ำตอนเน็ตช้า ไม่งั้นคำขอที่สองจะเจอ 409 แล้วขึ้นว่าเพิ่มไม่สำเร็จ
  // ทั้งที่คำขอแรกสร้างสำเร็จไปแล้ว
  const [busy, setBusy] = useState(false)

  const fail = (e) => setMsg({ type: 'error', text: translateError(t, e) })

  // error จากการโหลดรายการ แยกจาก error ของปุ่มที่ผู้ใช้กด
  // เพื่อจะได้ล้างทิ้งเองเมื่อโหลดรอบถัดไปสำเร็จ โดยไม่ไปลบข้อความอย่าง
  // "เพิ่มเครื่องแล้ว" ที่ผู้ใช้ยังอ่านไม่ทัน
  const failLoad = (e) => setMsg({ type: 'error', text: translateError(t, e), source: 'load' })
  const clearLoadError = () => setMsg((m) => (m?.source === 'load' ? null : m))

  const since = (iso) => {
    if (!iso) return t('reader.never')
    const secs = Math.floor((Date.now() - new Date(iso).getTime()) / 1000)
    if (secs < 60) return t('reader.secondsAgo', { n: secs })
    if (secs < 3600) return t('reader.minutesAgo', { n: Math.floor(secs / 60) })
    if (secs < 86400) return t('reader.hoursAgo', { n: Math.floor(secs / 3600) })
    return t('reader.daysAgo', { n: Math.floor(secs / 86400) })
  }

  const duration = (secs) => {
    const d = Math.floor(secs / 86400)
    const h = Math.floor((secs % 86400) / 3600)
    const m = Math.floor((secs % 3600) / 60)
    if (d > 0) return t('reader.durDH', { d, h })
    if (h > 0) return t('reader.durHM', { h, m })
    return t('reader.durM', { m })
  }

  const rc522Badge = (r) => {
    if (r.rc522_ok == null) {
      return <span className="badge off" title={t('reader.rc522Unknown')}>—</span>
    }
    const stale = secondsSince(r.last_seen_at) > STALE_SECS
    const tone = stale ? 'off' : r.rc522_ok ? 'ok' : 'deny'
    return (
      <span
        className={'badge ' + tone}
        title={stale ? t('reader.rc522Stale', { ago: since(r.last_seen_at) }) : undefined}
      >
        {r.rc522_ok ? t('reader.rc522Ok') : t('reader.rc522Error')}
      </span>
    )
  }

  // silent: การดึงอัตโนมัติทุก 5 วินาที ถ้าพังไม่ต้องขึ้นแถบแดงซ้ำ ๆ
  // เพราะ App มีแถบ "ต่อ API ไม่ได้" คอยบอกเรื่องการเชื่อมต่ออยู่แล้ว
  const load = async (off = offsetRef.current, silent = false) => {
    try {
      const page = await api.listReaders({ registered: true, limit: PAGE_SIZE, offset: off })
      setReaders(page.items)
      setTotal(page.total)
      clearLoadError()
      if (page.items.length === 0 && off > 0) {
        const back = Math.max(0, off - PAGE_SIZE)
        offsetRef.current = back
        setOffset(back)
        return load(back, silent)
      }
    } catch (e) {
      if (!silent) failLoad(e)
    }
  }

  const loadDiscovered = async () => {
    try {
      const page = await api.listReaders({ registered: false, limit: 50, offset: 0 })
      setDiscovered(page.items)
    } catch {
      // ไม่ต้องรบกวนหน้าจอ กล่องนี้แค่โชว์เฉย ๆ
    }
  }

  const refreshAll = (silent = false) => { load(undefined, silent); loadDiscovered() }

  const goTo = (off) => { offsetRef.current = off; setOffset(off); load(off) }

  useEffect(() => {
    load(0)
    loadDiscovered()
    // เครื่องที่เพิ่งเปิดจะประกาศตัวเข้ามา ให้โผล่เองโดยไม่ต้องรีเฟรชหน้า
    const timer = setInterval(() => refreshAll(true), 5000)
    return () => clearInterval(timer)
  }, [])

  const set = (k) => (e) => {
    const v = e.target.type === 'checkbox' ? e.target.checked : e.target.value
    setForm((f) => ({ ...f, [k]: v }))
  }

  // เลือกอุปกรณ์ที่พบ มากรอกลงฟอร์ม - รหัสกับ MAC มาจากตัวเครื่องเอง
  const useDevice = (d) => {
    setEditingId(null)
    setForm({
      code: d.code,
      mac: d.mac || '',
      name: d.name || '',
      location: d.location || '',
      note: d.note || '',
      active: true,
    })
    setMsg({ type: 'success', text: t('reader.picked', { code: d.code }) })
    window.scrollTo({ top: 0, behavior: 'smooth' })
  }

  const submit = async (e) => {
    e.preventDefault()
    if (busy) return
    setBusy(true)
    setMsg(null)
    try {
      if (editingId) {
        const { code, ...rest } = form
        await api.updateReader(editingId, rest)
        setMsg({ type: 'success', text: t('reader.saved') })
      } else {
        await api.createReader(form)
        setMsg({ type: 'success', text: t('reader.created', { code: form.code.toUpperCase() }) })
      }
      setForm(EMPTY)
      setEditingId(null)
      refreshAll()
      onChanged?.()
    } catch (e) {
      fail(e)
    } finally {
      setBusy(false)
    }
  }

  const startEdit = (r) => {
    setEditingId(r.id)
    setForm({
      code: r.code,
      mac: r.mac || '',
      name: r.name || '',
      location: r.location || '',
      note: r.note || '',
      active: r.active,
    })
  }

  const remove = async (r) => {
    if (!confirm(t('reader.confirmDelete', { code: r.code }))) return
    try {
      await api.deleteReader(r.id)
      refreshAll()
      onChanged?.()
    } catch (e) {
      fail(e)
    }
  }

  return (
    <>
      {msg && <div className={'msg ' + msg.type}>{msg.text}</div>}

      <div className="panel scan-live">
        <h2>{t('reader.discovered', { n: discovered.length })}</h2>
        <div className="hint" style={{ marginBottom: 12 }}>{t('reader.discoveredHint')}</div>

        {discovered.length === 0 ? (
          <div className="hint">{t('reader.discoveredNone')}</div>
        ) : (
          <div className="table-wrap">
            <table>
              <thead>
                <tr>
                  <th>{t('reader.code')}</th><th>{t('reader.mac')}</th>
                  <th>{t('reader.ip')}</th><th>{t('reader.lastSeen')}</th><th></th>
                </tr>
              </thead>
              <tbody>
                {discovered.map((d) => (
                  <tr key={d.id}>
                    <td className="mono"><strong>{d.code}</strong></td>
                    <td className="mono">{d.mac || '-'}</td>
                    <td className="mono">{d.last_ip || '-'}</td>
                    <td>{since(d.last_seen_at)}</td>
                    <td style={{ whiteSpace: 'nowrap' }}>
                      <button type="button" className="btn-sm" onClick={() => useDevice(d)}>
                        {t('reader.use')}
                      </button>
                    </td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
      </div>

      <form className="panel" onSubmit={submit}>
        <h2>{editingId ? t('reader.formEdit', { code: form.code }) : t('reader.formNew')}</h2>
        <div className="grid">
          <div>
            <label>{t('reader.code')} *</label>
            <input
              className="mono" value={form.code} onChange={set('code')}
              disabled={!!editingId} placeholder="WC09" required
            />
            <div className="hint">{t('reader.codeHint')}</div>
          </div>
          <div>
            <label>{t('reader.mac')}</label>
            <input className="mono" value={form.mac} onChange={set('mac')} placeholder="D8:F1:5B:12:D0:B8" />
            <div className="hint">{t('reader.macHint')}</div>
          </div>
          <div>
            <label>{t('reader.name')}</label>
            <input value={form.name} onChange={set('name')} placeholder={t('reader.namePlaceholder')} />
          </div>
          <div>
            <label>{t('reader.location')}</label>
            <input value={form.location} onChange={set('location')} placeholder={t('reader.locationPlaceholder')} />
          </div>
          <div>
            <label>{t('reader.note')}</label>
            <input value={form.note} onChange={set('note')} />
          </div>
          <div>
            <label>{t('reader.status')}</label>
            <div style={{ paddingTop: 9 }}>
              <label style={{ display: 'flex', gap: 8, alignItems: 'center', color: 'var(--text)' }}>
                <input type="checkbox" checked={form.active} onChange={set('active')} style={{ width: 16 }} />
                {t('reader.enabled')}
              </label>
              <div className="hint">{t('reader.disabledHint')}</div>
            </div>
          </div>
        </div>
        <div className="row" style={{ marginTop: 16 }}>
          <button className="primary" disabled={busy}>
            {editingId ? t('reader.save') : t('reader.add')}
          </button>
          {editingId && (
            <button type="button" className="ghost" onClick={() => { setEditingId(null); setForm(EMPTY) }}>
              {t('reader.cancel')}
            </button>
          )}
        </div>
      </form>

      <div className="panel">
        <h2>{t('reader.listTitle', { n: total })}</h2>
        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>{t('reader.code')}</th><th>{t('reader.mac')}</th><th>{t('reader.name')}</th>
                <th>{t('reader.location')}</th><th>{t('reader.status')}</th>
                <th>{t('reader.rc522')}</th>
                <th title={t('reader.recoveriesHint')}>{t('reader.recoveries')}</th>
                <th>{t('reader.firmware')}</th>
                <th>{t('reader.lastSeen')}</th><th>{t('reader.ip')}</th><th></th>
              </tr>
            </thead>
            <tbody>
              {readers.map((r) => (
                <tr key={r.id}>
                  <td className="mono"><strong>{r.code}</strong></td>
                  <td className="mono">{r.mac || '-'}</td>
                  <td>{r.name || '-'}</td>
                  <td>{r.location || '-'}</td>
                  <td>
                    <span className={'badge ' + (r.active ? 'ok' : 'off')}>
                      {r.active ? t('status.active') : t('status.off')}
                    </span>
                  </td>
                  <td>{rc522Badge(r)}</td>
                  <td title={t('reader.recoveriesHint')}>
                    {r.rc522_recoveries == null && !r.rc522_recoveries_total ? '-' : (
                      <>
                        <span className={'count' + (r.rc522_recoveries_total > 0 ? ' is-warn' : '')}>
                          {r.rc522_recoveries_total}
                        </span>
                        {r.rc522_recoveries != null && (
                          <div className="cell-hint">
                            {t('reader.recoveriesSinceBoot', { n: r.rc522_recoveries })}
                          </div>
                        )}
                      </>
                    )}
                  </td>
                  <td style={{ whiteSpace: 'nowrap' }}>
                    <span className="mono">{r.firmware || '-'}</span>
                    {r.uptime_s != null && (
                      <div className="cell-hint" title={t('reader.uptimeHint')}>
                        {t('reader.uptime', { d: duration(r.uptime_s) })}
                      </div>
                    )}
                  </td>
                  <td>{since(r.last_seen_at)}</td>
                  <td className="mono">{r.last_ip || '-'}</td>
                  <td style={{ whiteSpace: 'nowrap' }}>
                    <div className="table-actions">
                      <button className="btn-sm" onClick={() => startEdit(r)}>{t('reader.edit')}</button>
                      <button className="btn-sm is-danger" onClick={() => remove(r)}>{t('reader.delete')}</button>
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
          {readers.length === 0 && <div className="empty">{t('reader.empty')}</div>}
        </div>

        <Pagination total={total} limit={PAGE_SIZE} offset={offset} onChange={goTo} />
      </div>
    </>
  )
}
