#pragma once
/**
 * lcr_diag.h — 全局诊断打印开关（单编译单元内共享）
 * 默认 false（上电静默）。主程序在 setup() 里按 GPIO4 电平设置。
 * 所有被门控的打印都用 if (g_lcr_diag) 包裹，关闭时零输出。
 */
//#ifndef LCR_DIAG_H
//#define LCR_DIAG_H
static bool g_lcr_diag = true;
//#endif
