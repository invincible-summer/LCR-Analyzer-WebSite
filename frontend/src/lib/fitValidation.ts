import type { FitJob } from './fitTypes'
export function validateFitJob(job: FitJob): void {
 const integer=(x:number,min:number,max:number)=>Number.isInteger(x)&&x>=min&&x<=max
 const nonnegative=(x:number)=>Number.isFinite(x)&&x>=0
 if(![1,2,3].includes(job.try))throw Error('未知引擎')
 if(!integer(job.points.length,4,100000)||job.points.some(p=>!Number.isFinite(p.f)||p.f<=0||!Number.isFinite(p.re)||!Number.isFinite(p.im)))throw Error('测量需包含 4..100000 个有限复阻抗频点，频率必须为正')
 if(job.mode!==undefined&&!['Strict','Fast'].includes(job.mode))throw Error('未知搜索模式')
 if(!integer(job.budget??0,0,2147483647)||!nonnegative(job.seconds??0))throw Error('搜索预算不合法')
 if(job.try===1){
  if(!integer(job.exactN??1,1,12)||!integer(job.maxN??4,1,12)||!integer(job.topK??8,1,100))throw Error('器件数须为 1..12，Top-K 须为 1..100')
 }else if(job.try===2){
  if(!integer(job.components.length,1,8)||!integer(job.topK??8,1,100))throw Error('元件行数须为 1..8')
  let count=0
  for(const c of job.components){if(!['R','L','C'].includes(c.kind)||!Number.isFinite(c.value)||c.value<=0||!nonnegative(c.dcr)||(c.kind!=='L'&&c.dcr!==0)||!integer(c.count,1,8))throw Error('元件类型、数值、DCR 或数量不合法');count+=c.count}
  if(count>8)throw Error('Try2 最多支持 8 个元件')
  if(!nonnegative(job.tolerance??0)||(job.tolerance??0)>=1||!nonnegative(job.dcrTolerance??0))throw Error('容差须为 0..100%（不含 100%）')
  if(!(job.tolerance??0)&&(job.robust||(job.dcrTolerance??0)>0))throw Error('稳健拟合和 DCR 容差需要先启用元件容差')
 }else if(!integer(job.edges.length,1,32)||job.edges.some(e=>!integer(e.u,0,15)||!integer(e.v,0,15)||e.u===e.v||!['R','L','C'].includes(e.kind)))throw Error('拓扑须含 1..32 条 R/L/C 边，节点标签为 0..15，不能自环')
}
