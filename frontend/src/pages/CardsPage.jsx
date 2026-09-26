import { useEffect, useRef, useState } from 'react'
import { api } from '../api.js'
import { translateError, useLang } from '../i18n.jsx'
import Pagination from '../components/Pagination.jsx'

const EMPTY = { uid: '', barcode: '', holder_name: '', card_type: '', note: '', active: true }
const PAGE_SIZE = 20

export default function CardsPage({ onChanged }) {
  const { lang, t } = useLang()

  const [cards, setCards] = useState([])
  const [total, setTotal] = useState(0)
  const [offset, setOffset] = useState(0)
  const [form, setForm] = useState(EMPTY)
  const [editingId, setEditingId] = useState(null)
  const [search, setSearch] = useState('')
  const [msg, setMsg] = useState(null)
  const [busy, setBusy] = useState(false)

  // รับ UID จากเครื่องอ่านอัตโนมัติ
  const [autoFill, setAutoFill] = useState(true)
  const [lastScan, setLastScan] = useState(null)
  // จำ id ของรายการล่าสุดที่เคยเห็น เพื่อแยกว่าเป็นการแตะใหม่ ไม่ใช่ของเก่าที่ค้างอยู่
  const seenLogId = useRef(null)
  // อ่านค่าล่าสุดใน interval โดยไม่ต้อง re-subscribe ทุกครั้งที่ state เปลี่ยน
  const stateRef = useRef({})
  stateRef.current = { autoFill, editingId, t }

  const fail = (e) => setMsg({ type: 'error', text: translateError(t, e) })

  const load = async (q = search, off = offset) => {
    try {
      const page = await api.listCards({ q, limit: PAGE_SIZE, offset: off })
      setCards(page.items)
      setTotal(page.total)
      // ลบรายการสุดท้ายของหน้าท้าย ๆ แล้วหน้าว่าง ให้ถอยกลับไปหน้าก่อนหน้า
      if (page.items.length === 0 && off > 0) {
        const back = Math.max(0, off - PAGE_SIZE)
        setOffset(back)
        return load(q, back)
      }
    } catch (e) {
      fail(e)
    }
  }

  useEffect(() => { load('', 0) }, [])

  const goTo = (off) => { setOffset(off); load(search, off) }
  const searchFirstPage = (q) => { setOffset(0); load(q, 0) }

  // ตรวจรายการแตะบัตรล่าสุดทุก 2 วินาที
  useEffect(() => {
    let alive = true

    const poll = async () => {
      try {
        const last = await api.latestLog()
        if (!alive || !last) return

        // ครั้งแรกแค่จำค่าไว้เป็นจุดเริ่ม ไม่เอาของเก่ามากรอกให้
        if (seenLogId.current === null) {
          seenLogId.current = last.id
          setLastScan(last)
          return
        }
        if (last.id === seenLogId.current) return

        seenLogId.current = last.id
        setLastScan(last)

        const { autoFill: auto, editingId: editing, t: tr } = stateRef.current
        if (!auto || editing) return

        if (last.card_id !== null) {
          // บัตรนี้ลงทะเบียนแล้ว ไม่กรอกทับฟอร์ม แค่บอกให้รู้
          setMsg({
            type: 'success',
            text: tr('scan.already', { uid: last.uid, name: last.holder_name || '-' }),
          })
          return
        }

        setForm((f) => ({ ...f, uid: last.uid, card_type: last.card_type || f.card_type }))
        setMsg({
          type: 'success',
          text: tr('scan.received', { uid: last.uid, reader: last.reader_code || '-' }),
        })
      } catch {
        // เงียบไว้ ไม่รบกวนหน้าจอ เพราะ poll ทุก 2 วินาที
      }
    }

    poll()
    const timer = setInterval(poll, 2000)
    return () => { alive = false; clearInterval(timer) }
  }, [])

  const set = (k) => (e) => {
    const v = e.target.type === 'checkbox' ? e.target.checked : e.target.value
    setForm((f) => ({ ...f, [k]: v }))
  }

  const useScan = (scan) => {
    if (!scan) return
    setForm((f) => ({ ...f, uid: scan.uid, card_type: scan.card_type || f.card_type }))
    setMsg({ type: 'success', text: t('scan.filled', { uid: scan.uid }) })
  }

  const submit = async (e) => {
    e.preventDefault()
    if (busy) return
    setBusy(true)
    setMsg(null)
    try {
      if (editingId) {
        const { uid, ...rest } = form
        await api.updateCard(editingId, rest)
        setMsg({ type: 'success', text: t('card.saved') })
      } else {
        await api.createCard(form)
        setMsg({ type: 'success', text: t('card.created', { uid: form.uid }) })
      }
      setForm(EMPTY)
      setEditingId(null)
      await load()
      onChanged?.()
    } catch (e) {
      fail(e)
    } finally {
      setBusy(false)
    }
  }

  const startEdit = (c) => {
    setEditingId(c.id)
    setForm({
      uid: c.uid,
      barcode: c.barcode || '',
      holder_name: c.holder_name,
      card_type: c.card_type || '',
      note: c.note || '',
      active: c.active,
    })
    window.scrollTo({ top: 0, behavior: 'smooth' })
  }

  const remove = async (c) => {
    if (!confirm(t('card.confirmDelete', { uid: c.uid, name: c.holder_name }))) return
    try {
      await api.deleteCard(c.id)
      await load()
      onChanged?.()
    } catch (e) {
      fail(e)
    }
  }

  const timeText = (iso) =>
    new Date(iso).toLocaleTimeString(lang === 'th' ? 'th-TH' : 'en-GB', { hour12: false })

  return (
    <>
      {msg && <div className={'msg ' + msg.type}>{msg.text}</div>}

      <div className="panel scan-live">
        <h2>{t('scan.title')}</h2>
        {lastScan ? (
          <div className="row" style={{ alignItems: 'center' }}>
            <div className="scan-uid mono">{lastScan.uid}</div>
            <div style={{ flex: '1 1 200px' }}>
              <div>
                {t('scan.reader')} <strong className="mono">{lastScan.reader_code || '-'}</strong>
                {' · '}{lastScan.card_type || t('scan.unknownType')}
                {' · '}{timeText(lastScan.scanned_at)}
              </div>
              <div className="hint">
                {lastScan.card_id !== null
                  ? `${t('scan.registered')} ${lastScan.holder_name || '-'}`
                  : t('scan.notRegistered')}
              </div>
            </div>
            <button type="button" className="ghost" onClick={() => useScan(lastScan)}>
              {t('scan.use')}
            </button>
          </div>
        ) : (
          <div className="hint">{t('scan.none')}</div>
        )}
        <label style={{ display: 'flex', gap: 8, alignItems: 'center', color: 'var(--text)', marginTop: 12 }}>
          <input
            type="checkbox" checked={autoFill} style={{ width: 16 }}
            onChange={(e) => setAutoFill(e.target.checked)}
          />
          {t('scan.autoFill')}
        </label>
      </div>

      <form className="panel" onSubmit={submit}>
        <h2>{editingId ? t('card.formEdit') : t('card.formNew')}</h2>
        <div className="grid">
          <div>
            <label>{t('card.uid')} *</label>
            <input
              className="mono" value={form.uid} onChange={set('uid')}
              disabled={!!editingId} placeholder={t('card.uidPlaceholder')} required
            />
          </div>
          <div>
            <label>{t('card.barcode')}</label>
            <input className="mono" value={form.barcode} onChange={set('barcode')} placeholder="GOC26080323.1B" />
          </div>
          <div>
            <label>{t('card.holder')} *</label>
            <input value={form.holder_name} onChange={set('holder_name')} required />
          </div>
          <div>
            <label>{t('card.type')}</label>
            <input value={form.card_type} onChange={set('card_type')} placeholder="MIFARE 1KB" />
          </div>
          <div>
            <label>{t('card.note')}</label>
            <input value={form.note} onChange={set('note')} />
          </div>
          <div>
            <label>{t('card.status')}</label>
            <div style={{ paddingTop: 9 }}>
              <label style={{ display: 'flex', gap: 8, alignItems: 'center', color: 'var(--text)' }}>
                <input type="checkbox" checked={form.active} onChange={set('active')} style={{ width: 16 }} />
                {t('card.enabled')}
              </label>
            </div>
          </div>
        </div>

        <div className="row" style={{ marginTop: 16 }}>
          <button className="primary" disabled={busy}>
            {editingId ? t('card.save') : t('card.register')}
          </button>
          {editingId ? (
            <button type="button" className="ghost" onClick={() => { setEditingId(null); setForm(EMPTY) }}>
              {t('card.cancel')}
            </button>
          ) : (
            <button type="button" className="ghost" onClick={() => setForm(EMPTY)}>
              {t('card.clear')}
            </button>
          )}
        </div>
      </form>

      <div className="panel">
        <h2>{t('card.listTitle', { n: total })}</h2>
        <div className="row" style={{ marginBottom: 14 }}>
          <div style={{ flex: '1 1 260px' }}>
            <input
              placeholder={t('card.searchPlaceholder')}
              value={search}
              onChange={(e) => setSearch(e.target.value)}
              onKeyDown={(e) => e.key === 'Enter' && searchFirstPage(search)}
            />
          </div>
          <button type="button" className="ghost" onClick={() => searchFirstPage(search)}>{t('card.search')}</button>
          <button type="button" className="ghost" onClick={() => { setSearch(''); searchFirstPage('') }}>
            {t('card.reset')}
          </button>
        </div>

        <div className="table-wrap">
          <table>
            <thead>
              <tr>
                <th>{t('card.uid')}</th><th>{t('log.barcode')}</th><th>{t('card.holder')}</th>
                <th>{t('card.type')}</th><th>{t('card.status')}</th><th>{t('card.note')}</th><th></th>
              </tr>
            </thead>
            <tbody>
              {cards.map((c) => (
                <tr key={c.id}>
                  <td className="mono">{c.uid}</td>
                  <td className="mono">{c.barcode || '-'}</td>
                  <td>{c.holder_name}</td>
                  <td>{c.card_type || '-'}</td>
                  <td>
                    <span className={'badge ' + (c.active ? 'ok' : 'off')}>
                      {c.active ? t('status.active') : t('status.off')}
                    </span>
                  </td>
                  <td>{c.note || '-'}</td>
                  <td style={{ whiteSpace: 'nowrap' }}>
                    <div className="table-actions">
                      <button className="btn-sm" onClick={() => startEdit(c)}>{t('card.edit')}</button>
                      <button className="btn-sm is-danger" onClick={() => remove(c)}>{t('card.delete')}</button>
                    </div>
                  </td>
                </tr>
              ))}
            </tbody>
          </table>
          {cards.length === 0 && <div className="empty">{t('card.empty')}</div>}
        </div>

        <Pagination total={total} limit={PAGE_SIZE} offset={offset} onChange={goTo} />
      </div>
    </>
  )
}
