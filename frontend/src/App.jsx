import { useCallback, useEffect, useState } from 'react'
import { api } from './api.js'
import { LANGS, useLang } from './i18n.jsx'
import CardsPage from './pages/CardsPage.jsx'
import ReadersPage from './pages/ReadersPage.jsx'
import LogsPage from './pages/LogsPage.jsx'

const TABS = [
  { id: 'cards', key: 'tab.cards' },
  { id: 'readers', key: 'tab.readers' },
  { id: 'logs', key: 'tab.logs' },
]

export default function App() {
  const { lang, setLang, t } = useLang()
  const [tab, setTab] = useState('cards')
  const [stats, setStats] = useState(null)
  const [offline, setOffline] = useState(false)

  const loadStats = useCallback(async () => {
    try {
      setStats(await api.stats())
      setOffline(false)
    } catch {
      setOffline(true)
    }
  }, [])

  useEffect(() => {
    loadStats()
    const timer = setInterval(loadStats, 5000)
    return () => clearInterval(timer)
  }, [loadStats])

  return (
    <div className="app">
      <header className="top">
        <h1>{t('app.title')}</h1>
        <nav>
          {TABS.map((item) => (
            <button
              key={item.id}
              className={tab === item.id ? 'active' : ''}
              onClick={() => setTab(item.id)}
            >
              {t(item.key)}
            </button>
          ))}
        </nav>
        <div className="lang-switch">
          {LANGS.map((l) => (
            <button
              key={l.id}
              className={lang === l.id ? 'active' : ''}
              onClick={() => setLang(l.id)}
              title={l.id === 'en' ? 'English' : 'ภาษาไทย'}
            >
              {l.label}
            </button>
          ))}
        </div>
      </header>

      {offline && <div className="msg error">{t('app.offline')}</div>}

      {stats && (
        <div className="stats">
          <div className="stat"><div className="v">{stats.total_cards}</div><div className="k">{t('stat.totalCards')}</div></div>
          <div className="stat"><div className="v">{stats.active_cards}</div><div className="k">{t('stat.activeCards')}</div></div>
          <div className="stat"><div className="v">{stats.total_readers}</div><div className="k">{t('stat.readers')}</div></div>
          <div className="stat"><div className="v">{stats.scans_today}</div><div className="k">{t('stat.scansToday')}</div></div>
          <div className="stat">
            <div className="v" style={{ color: stats.denied_today ? 'var(--deny)' : undefined }}>
              {stats.denied_today}
            </div>
            <div className="k">{t('stat.deniedToday')}</div>
          </div>
        </div>
      )}

      {tab === 'cards' && <CardsPage onChanged={loadStats} />}
      {tab === 'readers' && <ReadersPage onChanged={loadStats} />}
      {tab === 'logs' && <LogsPage />}
    </div>
  )
}
