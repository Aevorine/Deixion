// 截图视图：图像按比例居中铺满容器，上面叠一层按百分比定位的图层（框、点、网格都画在图层里）。
import { h } from './dom.js';

export function shotView() {
  const img = h('img', { class: 'shot-img', alt: '', draggable: 'false' });
  const layer = h('div', { class: 'shot-layer' });
  const shot = h('div', { class: 'shot', style: { display: 'none' } }, img, layer);
  const stage = h('div', { class: 'shot-stage' }, shot);
  let ratio = 16 / 9;
  const fit = () => {
    const r = stage.getBoundingClientRect();
    const w = Math.min(r.width, r.height * ratio);
    shot.style.width = `${Math.max(0, w)}px`;
    shot.style.height = `${Math.max(0, w / ratio)}px`;
  };
  new ResizeObserver(fit).observe(stage);
  return {
    root: stage,
    shot,
    layer,
    img,
    set(mime, b64, w, hgt) {
      img.src = `data:${mime};base64,${b64}`;
      ratio = w / hgt;
      shot.style.display = '';
      fit();
    },
    /** 指针位置 → 图像内 0..1 比例；在图像外返回 null。 */
    frac(e) {
      const r = shot.getBoundingClientRect();
      const u = (e.clientX - r.left) / r.width, v = (e.clientY - r.top) / r.height;
      return u < 0 || u > 1 || v < 0 || v > 1 ? null : { u, v };
    },
  };
}
