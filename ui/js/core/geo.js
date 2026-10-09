// Meridian 的前端镜像：与原生 meridian.cpp 的层级码完全一致（Morton 交织 + Crockford Base32，1–6 级）。
const ALPHA = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
export const MAX_LEVEL = 6;

const q16 = (v) => Math.min(65535, Math.floor(Math.min(1, Math.max(0, v)) * 65536));
function spread(v) {
  v &= 0xffff;
  v = (v | (v << 8)) & 0x00ff00ff;
  v = (v | (v << 4)) & 0x0f0f0f0f;
  v = (v | (v << 2)) & 0x33333333;
  v = (v | (v << 1)) & 0x55555555;
  return v >>> 0;
}

/** (λ,φ) → 32 位 Morton 键：λ 占奇数位，φ 占偶数位。 */
export const mortonKey = (lam, phi) => (((spread(q16(lam)) << 1) >>> 0) | spread(q16(phi))) >>> 0;

export function codeOf(lam, phi, level) {
  level = Math.min(MAX_LEVEL, Math.max(1, level | 0));
  const key35 = mortonKey(lam, phi) * 8;
  let s = '';
  for (let i = 0; i < level; i++) s += ALPHA[Math.floor(key35 / 2 ** (35 - 5 * (i + 1))) % 32];
  return s;
}

export function decode(code) {
  code = String(code || '').toUpperCase().replace(/[IL]/g, '1').replace(/O/g, '0');
  if (!code || code.length > MAX_LEVEL) return null;
  let bits = 0n;
  for (const c of code) {
    const v = ALPHA.indexOf(c);
    if (v < 0) return null;
    bits = (bits << 5n) | BigInt(v);
  }
  const n = code.length * 5;
  let xv = 0, yv = 0, xb = 0, yb = 0;
  for (let i = 0; i < n; i++) {
    const bit = Number((bits >> BigInt(n - 1 - i)) & 1n);
    if (i % 2 === 0) { xv = xv * 2 + bit; xb++; } else { yv = yv * 2 + bit; yb++; }
  }
  const wx = 1 / 2 ** xb, wy = 1 / 2 ** yb;
  return { level: code.length, a: { lam: xv * wx, phi: yv * wy }, b: { lam: (xv + 1) * wx, phi: (yv + 1) * wy }, center: { lam: (xv + 0.5) * wx, phi: (yv + 0.5) * wy } };
}

/** 某层级单个格子的边长（占整个平面的比例）。 */
export const cellSize = (level) => ({ w: 1 / 2 ** Math.ceil((5 * level) / 2), h: 1 / 2 ** Math.floor((5 * level) / 2) });

export const parseLL = (s) => {
  if (s && typeof s === 'object') return { lam: +s.lam, phi: +s.phi };
  const m = String(s || '').split(/[,\s]+/).map(Number);
  return m.length >= 2 && m.every(Number.isFinite) ? { lam: m[0], phi: m[1] } : null;
};
export const fmtLL = (p, d = 4) => `${p.lam.toFixed(d)}, ${p.phi.toFixed(d)}`;
