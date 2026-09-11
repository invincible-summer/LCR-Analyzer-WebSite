// crc32.ts — CRC-32/ISO-HDLC（与固件 crc32Of 位位一致）
//
// 反射多项式 0xEDB88320、init/xorout 0xFFFFFFFF —— 即 zlib 的 crc32()。
// 固件侧实现见 ino/LCR_UI/dataset.cpp；双侧由
// "123456789" → 0xCBF43926 标准向量锁定（vitest + ino/test/test_csv.cpp）。

const TABLE = (() => {
  const t = new Uint32Array(256)
  for (let i = 0; i < 256; i++) {
    let c = i
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1
    t[i] = c >>> 0
  }
  return t
})()

export function crc32(bytes: Uint8Array): number {
  let c = 0xffffffff
  for (let i = 0; i < bytes.length; i++) c = TABLE[(c ^ bytes[i]) & 0xff] ^ (c >>> 8)
  return (c ^ 0xffffffff) >>> 0
}
