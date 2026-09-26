import { useLang } from '../i18n.jsx'

/*
 * แถบแบ่งหน้าที่ใช้ร่วมกันทั้ง 3 หน้า
 * รับ offset/total เป็นหน่วยแถว ไม่ใช่หมายเลขหน้า เพราะ API คุยด้วย limit/offset
 */
export default function Pagination({ total, limit, offset, onChange }) {
  const { t } = useLang()

  const pages = Math.max(1, Math.ceil(total / limit))
  const current = Math.floor(offset / limit) + 1
  const from = total === 0 ? 0 : offset + 1
  const to = Math.min(offset + limit, total)

  const go = (page) => {
    const clamped = Math.min(Math.max(1, page), pages)
    onChange((clamped - 1) * limit)
  }

  // หน้าเดียวจบก็ไม่ต้องโชว์ปุ่ม แต่ยังบอกจำนวนไว้ให้รู้
  if (pages <= 1) {
    return <div className="pager"><span className="pager-info">{t('pager.total', { n: total })}</span></div>
  }

  return (
    <div className="pager">
      <span className="pager-info">{t('pager.showing', { from, to, total })}</span>
      <div className="pager-buttons">
        <button type="button" className="ghost" disabled={current === 1} onClick={() => go(1)}>
          {t('pager.first')}
        </button>
        <button type="button" className="ghost" disabled={current === 1} onClick={() => go(current - 1)}>
          {t('pager.prev')}
        </button>
        <span className="pager-current">{t('pager.page', { page: current, pages })}</span>
        <button type="button" className="ghost" disabled={current === pages} onClick={() => go(current + 1)}>
          {t('pager.next')}
        </button>
        <button type="button" className="ghost" disabled={current === pages} onClick={() => go(pages)}>
          {t('pager.last')}
        </button>
      </div>
    </div>
  )
}
