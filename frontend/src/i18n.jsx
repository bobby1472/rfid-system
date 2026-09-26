import { createContext, useCallback, useContext, useEffect, useState } from 'react'

export const LANGS = [
  { id: 'en', label: 'EN' },
  { id: 'th', label: 'ไทย' },
]

const DEFAULT_LANG = 'en'
const STORAGE_KEY = 'rfid.lang'

const MESSAGES = {
  en: {
    'app.title': 'RFID System',
    'tab.cards': 'Cards',
    'tab.readers': 'Readers',
    'tab.logs': 'Access log',

    'stat.totalCards': 'Cards',
    'stat.activeCards': 'Active cards',
    'stat.readers': 'Readers',
    'stat.scansToday': 'Scans today',
    'stat.deniedToday': 'Denied today',

    'app.offline': 'Cannot reach the API — check that the backend is running on port 7777',

    'scan.title': 'Last card scanned',
    'scan.none': 'No scans yet — tap a card on the reader and it will appear here',
    'scan.reader': 'Reader',
    'scan.unknownType': 'unknown type',
    'scan.registered': 'Registered to',
    'scan.notRegistered': 'Not registered',
    'scan.use': 'Use this UID',
    'scan.autoFill': 'Fill the UID into the form automatically on the next scan',
    'scan.received': 'Received UID {uid} from reader {reader}',
    'scan.already': 'Card {uid} is already registered to {name}',
    'scan.filled': 'UID {uid} put into the form',

    'card.formNew': 'Register a new card',
    'card.formEdit': 'Edit card',
    'card.uid': 'UID',
    'card.uidPlaceholder': 'tap a card on the reader',
    'card.barcode': 'Barcode on the card',
    'card.holder': 'Card holder',
    'card.type': 'Card type',
    'card.note': 'Note',
    'card.status': 'Status',
    'card.enabled': 'Enabled (access allowed)',
    'card.register': 'Register card',
    'card.save': 'Save changes',
    'card.cancel': 'Cancel',
    'card.clear': 'Clear form',
    'card.listTitle': 'Registered cards ({n})',
    'card.searchPlaceholder': 'Search by UID, name or barcode',
    'card.search': 'Search',
    'card.reset': 'Reset',
    'card.empty': 'No cards registered yet',
    'card.edit': 'Edit',
    'card.delete': 'Delete',
    'card.saved': 'Changes saved',
    'card.created': 'Card {uid} registered — tap it again to test',
    'card.confirmDelete': 'Delete card {uid} ({name})?\nThe access history will be kept.',

    'reader.formNew': 'Add a reader',
    'reader.formEdit': 'Edit reader {code}',
    'reader.code': 'Reader code',
    'reader.codeHint': 'Must match DEVICE_ID in the firmware',
    'reader.name': 'Display name',
    'reader.namePlaceholder': 'Front gate',
    'reader.location': 'Location',
    'reader.locationPlaceholder': 'Building A, floor 1',
    'reader.note': 'Note',
    'reader.status': 'Status',
    'reader.enabled': 'Enabled',
    'reader.disabledHint': 'When disabled this reader rejects every card',
    'reader.add': 'Add reader',
    'reader.save': 'Save',
    'reader.cancel': 'Cancel',
    'reader.listTitle': 'Readers ({n})',
    'reader.autoHint': 'A reader that reports in before being registered is added automatically — you can name it afterwards',
    'reader.lastSeen': 'Last seen',
    'reader.ip': 'IP',
    'reader.empty': 'No readers yet',
    'reader.edit': 'Edit',
    'reader.delete': 'Delete',
    'reader.saved': 'Saved',
    'reader.created': 'Reader {code} added',
    'reader.confirmDelete': 'Delete reader {code}?\nThe access history will be kept.',
    'reader.mac': 'MAC address',
    'reader.macHint': 'Filled in automatically when the device reports in',
    'reader.discovered': 'Devices found on the network ({n})',
    'reader.discoveredHint': 'A device that has powered on and reached the server but has not been registered yet. Pick one to fill the form below.',
    'reader.discoveredNone': 'No new devices. Power a NodeMCU on and it will appear here within a few seconds of joining WiFi.',
    'reader.use': 'Use this device',
    'reader.picked': 'Loaded device {code} into the form',
    'reader.firmware': 'Firmware',
    'reader.never': 'never connected',
    'reader.secondsAgo': '{n}s ago',
    'reader.minutesAgo': '{n}m ago',
    'reader.hoursAgo': '{n}h ago',
    'reader.daysAgo': '{n}d ago',

    'log.title': 'Access log ({n} entries)',
    'log.uid': 'UID',
    'log.reader': 'Reader',
    'log.result': 'Result',
    'log.all': 'All',
    'log.allowed': 'Allowed',
    'log.denied': 'Denied',
    'log.refresh': 'Refresh',
    'log.export': 'Export Excel',
    'log.live': 'Auto refresh',
    'log.time': 'Time',
    'log.barcode': 'Barcode',
    'log.holder': 'Card holder',
    'log.type': 'Type',
    'log.reason': 'Reason',
    'log.empty': 'No entries yet — tap a card on the reader',
    'log.unknownHolder': 'unknown',

    'reason.ok': 'Allowed',
    'reason.unknown_card': 'Card not registered',
    'reason.card_disabled': 'Card disabled',
    'reason.reader_disabled': 'Reader disabled',

    'status.active': 'Active',
    'status.off': 'Off',

    'pager.total': '{n} entries in total',
    'pager.showing': 'Showing {from}-{to} of {total}',
    'pager.first': 'First',
    'pager.prev': 'Previous',
    'pager.next': 'Next',
    'pager.last': 'Last',
    'pager.page': 'Page {page} / {pages}',

    'err.duplicate_uid': 'UID {uid} is already registered',
    'err.duplicate_barcode': 'Barcode {barcode} is already used by another card',
    'err.duplicate_reader_code': 'A reader with code {code} already exists',
    'err.duplicate_mac': 'This MAC address is already used by another reader',
    'err.conflict': 'This record conflicts with an existing one',
    'err.card_not_found': 'Card not found',
    'err.reader_not_found': 'Reader not found',
    'err.noLatest': 'No scans recorded yet',
    'err.generic': 'Something went wrong ({status})',
  },

  th: {
    'app.title': 'RFID System',
    'tab.cards': 'บัตร',
    'tab.readers': 'เครื่องอ่าน',
    'tab.logs': 'ประวัติเข้าออก',

    'stat.totalCards': 'บัตรทั้งหมด',
    'stat.activeCards': 'บัตรที่ใช้งานได้',
    'stat.readers': 'เครื่องอ่าน',
    'stat.scansToday': 'แตะบัตรวันนี้',
    'stat.deniedToday': 'ถูกปฏิเสธวันนี้',

    'app.offline': 'ต่อ API ไม่ได้ — ตรวจว่ารัน backend อยู่หรือเปล่า (พอร์ต 7777)',

    'scan.title': 'บัตรที่แตะล่าสุด',
    'scan.none': 'ยังไม่มีการแตะบัตร — ลองแตะที่เครื่องอ่าน แล้วข้อมูลจะขึ้นที่นี่เอง',
    'scan.reader': 'เครื่อง',
    'scan.unknownType': 'ไม่ทราบชนิด',
    'scan.registered': 'ลงทะเบียนแล้ว:',
    'scan.notRegistered': 'ยังไม่ได้ลงทะเบียน',
    'scan.use': 'ใช้ UID นี้',
    'scan.autoFill': 'กรอก UID ลงฟอร์มอัตโนมัติเมื่อมีการแตะบัตรใบใหม่',
    'scan.received': 'รับ UID {uid} จากเครื่อง {reader} แล้ว',
    'scan.already': 'บัตร {uid} ลงทะเบียนไว้แล้วในชื่อ {name}',
    'scan.filled': 'ใส่ UID {uid} ในฟอร์มแล้ว',

    'card.formNew': 'ลงทะเบียนบัตรใหม่',
    'card.formEdit': 'แก้ไขบัตร',
    'card.uid': 'UID',
    'card.uidPlaceholder': 'แตะบัตรที่เครื่องอ่าน',
    'card.barcode': 'บาร์โค้ดบนบัตร',
    'card.holder': 'ชื่อผู้ถือบัตร',
    'card.type': 'ชนิดบัตร',
    'card.note': 'หมายเหตุ',
    'card.status': 'สถานะ',
    'card.enabled': 'เปิดใช้งาน (ผ่านได้)',
    'card.register': 'ลงทะเบียนบัตร',
    'card.save': 'บันทึกการแก้ไข',
    'card.cancel': 'ยกเลิก',
    'card.clear': 'ล้างฟอร์ม',
    'card.listTitle': 'บัตรที่ลงทะเบียนไว้ ({n})',
    'card.searchPlaceholder': 'ค้นหาจาก UID ชื่อ หรือบาร์โค้ด',
    'card.search': 'ค้นหา',
    'card.reset': 'ล้าง',
    'card.empty': 'ยังไม่มีบัตรที่ลงทะเบียน',
    'card.edit': 'แก้ไข',
    'card.delete': 'ลบ',
    'card.saved': 'บันทึกการแก้ไขแล้ว',
    'card.created': 'ลงทะเบียนบัตร {uid} แล้ว - แตะบัตรอีกครั้งเพื่อทดสอบ',
    'card.confirmDelete': 'ลบบัตร {uid} ({name}) ?\nประวัติการเข้าออกจะยังอยู่ครบ',

    'reader.formNew': 'เพิ่มเครื่องอ่าน',
    'reader.formEdit': 'แก้ไขเครื่อง {code}',
    'reader.code': 'รหัสเครื่อง',
    'reader.codeHint': 'ต้องตรงกับค่า DEVICE_ID ในเฟิร์มแวร์',
    'reader.name': 'ชื่อเรียก',
    'reader.namePlaceholder': 'ประตูหน้าโรงงาน',
    'reader.location': 'ตำแหน่งติดตั้ง',
    'reader.locationPlaceholder': 'อาคาร A ชั้น 1',
    'reader.note': 'หมายเหตุ',
    'reader.status': 'สถานะ',
    'reader.enabled': 'เปิดใช้งาน',
    'reader.disabledHint': 'ถ้าปิด เครื่องนี้จะปฏิเสธทุกบัตร',
    'reader.add': 'เพิ่มเครื่อง',
    'reader.save': 'บันทึก',
    'reader.cancel': 'ยกเลิก',
    'reader.listTitle': 'เครื่องอ่านในระบบ ({n})',
    'reader.autoHint': 'เครื่องที่ยิงข้อมูลเข้ามาโดยยังไม่ได้ลงทะเบียน จะถูกเพิ่มให้อัตโนมัติ แล้วค่อยมาตั้งชื่อทีหลังได้',
    'reader.lastSeen': 'เห็นล่าสุด',
    'reader.ip': 'IP',
    'reader.empty': 'ยังไม่มีเครื่องอ่านในระบบ',
    'reader.edit': 'แก้ไข',
    'reader.delete': 'ลบ',
    'reader.saved': 'บันทึกแล้ว',
    'reader.created': 'เพิ่มเครื่อง {code} แล้ว',
    'reader.confirmDelete': 'ลบเครื่อง {code} ?\nประวัติการแตะบัตรจะยังอยู่ครบ',
    'reader.mac': 'MAC address',
    'reader.macHint': 'ระบบกรอกให้เองเมื่ออุปกรณ์ติดต่อเข้ามา',
    'reader.discovered': 'อุปกรณ์ที่พบในเครือข่าย ({n})',
    'reader.discoveredHint': 'อุปกรณ์ที่เปิดเครื่องและติดต่อเซิร์ฟเวอร์ได้แล้ว แต่ยังไม่ได้ลงทะเบียน เลือกเครื่องเพื่อกรอกลงฟอร์มด้านล่าง',
    'reader.discoveredNone': 'ยังไม่พบอุปกรณ์ใหม่ เสียบไฟ NodeMCU แล้วรอไม่กี่วินาทีหลังต่อ WiFi ติด เครื่องจะโผล่ที่นี่เอง',
    'reader.use': 'ใช้อุปกรณ์นี้',
    'reader.picked': 'ใส่ข้อมูลของเครื่อง {code} ลงฟอร์มแล้ว',
    'reader.firmware': 'เฟิร์มแวร์',
    'reader.never': 'ยังไม่เคยเชื่อมต่อ',
    'reader.secondsAgo': '{n} วินาทีที่แล้ว',
    'reader.minutesAgo': '{n} นาทีที่แล้ว',
    'reader.hoursAgo': '{n} ชั่วโมงที่แล้ว',
    'reader.daysAgo': '{n} วันที่แล้ว',

    'log.title': 'ประวัติการเข้าออก ({n} รายการ)',
    'log.uid': 'UID',
    'log.reader': 'เครื่องอ่าน',
    'log.result': 'ผลลัพธ์',
    'log.all': 'ทั้งหมด',
    'log.allowed': 'ผ่าน',
    'log.denied': 'ไม่ผ่าน',
    'log.refresh': 'รีเฟรช',
    'log.export': 'ส่งออก Excel',
    'log.live': 'อัปเดตอัตโนมัติ',
    'log.time': 'เวลา',
    'log.barcode': 'บาร์โค้ด',
    'log.holder': 'ชื่อผู้ถือ',
    'log.type': 'ชนิด',
    'log.reason': 'เหตุผล',
    'log.empty': 'ยังไม่มีประวัติ ลองแตะบัตรที่เครื่องอ่าน',
    'log.unknownHolder': 'ไม่รู้จัก',

    'reason.ok': 'ผ่าน',
    'reason.unknown_card': 'บัตรไม่อยู่ในระบบ',
    'reason.card_disabled': 'บัตรถูกปิดใช้งาน',
    'reason.reader_disabled': 'เครื่องถูกปิดใช้งาน',

    'status.active': 'ใช้งาน',
    'status.off': 'ปิด',

    'pager.total': 'ทั้งหมด {n} รายการ',
    'pager.showing': 'แสดง {from}-{to} จาก {total} รายการ',
    'pager.first': 'หน้าแรก',
    'pager.prev': 'ก่อนหน้า',
    'pager.next': 'ถัดไป',
    'pager.last': 'หน้าสุดท้าย',
    'pager.page': 'หน้า {page} / {pages}',

    'err.duplicate_uid': 'UID {uid} ถูกลงทะเบียนไว้แล้ว',
    'err.duplicate_barcode': 'บาร์โค้ด {barcode} ถูกใช้กับบัตรใบอื่นแล้ว',
    'err.duplicate_reader_code': 'มีเครื่อง {code} อยู่แล้ว',
    'err.duplicate_mac': 'MAC นี้ถูกใช้กับเครื่องอ่านตัวอื่นแล้ว',
    'err.conflict': 'ข้อมูลซ้ำกับรายการที่มีอยู่แล้ว',
    'err.card_not_found': 'ไม่พบบัตรนี้',
    'err.reader_not_found': 'ไม่พบเครื่องอ่านนี้',
    'err.noLatest': 'ยังไม่มีประวัติการแตะบัตร',
    'err.generic': 'เกิดข้อผิดพลาด ({status})',
  },
}

const LangContext = createContext(null)

function readStored() {
  // localStorage โยน error ได้ในโหมดส่วนตัวหรือเมื่อบล็อกคุกกี้ไว้
  try {
    const v = localStorage.getItem(STORAGE_KEY)
    return LANGS.some((l) => l.id === v) ? v : DEFAULT_LANG
  } catch {
    return DEFAULT_LANG
  }
}

export function LangProvider({ children }) {
  const [lang, setLangState] = useState(readStored)

  const setLang = useCallback((next) => {
    setLangState(next)
    try {
      localStorage.setItem(STORAGE_KEY, next)
    } catch {
      // จำไม่ได้ก็ไม่เป็นไร แค่ต้องเลือกใหม่ทุกครั้งที่เปิดหน้า
    }
  }, [])

  useEffect(() => {
    document.documentElement.lang = lang
  }, [lang])

  const t = useCallback(
    (key, params) => {
      const template = MESSAGES[lang]?.[key] ?? MESSAGES[DEFAULT_LANG][key] ?? key
      if (!params) return template
      return template.replace(/\{(\w+)\}/g, (_, name) =>
        params[name] === undefined ? `{${name}}` : String(params[name]),
      )
    },
    [lang],
  )

  return <LangContext.Provider value={{ lang, setLang, t }}>{children}</LangContext.Provider>
}

export function useLang() {
  const ctx = useContext(LangContext)
  if (!ctx) throw new Error('useLang must be used inside <LangProvider>')
  return ctx
}

/** แปลง error จาก api.js ให้เป็นข้อความในภาษาที่เลือก */
export function translateError(t, err) {
  // fetch() โยน TypeError เมื่อต่อเซิร์ฟเวอร์ไม่ได้เลย (เช่นตอน pm2 restart)
  // ข้อความดิบคือ "Failed to fetch" ซึ่งผู้ใช้อ่านแล้วไม่รู้ว่าต้องทำอะไร
  if (isNetworkError(err)) return t('app.offline')
  if (err?.code) {
    const key = `err.${err.code}`
    const text = t(key, err.params)
    if (text !== key) return text
  }
  return err?.message || t('err.generic', { status: err?.status ?? '?' })
}

/** ต่อเซิร์ฟเวอร์ไม่ได้เลย ต่างจาก API ตอบกลับมาว่าผิดพลาด */
export function isNetworkError(err) {
  return err instanceof TypeError && !err.status
}
