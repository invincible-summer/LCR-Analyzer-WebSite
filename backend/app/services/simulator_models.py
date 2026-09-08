"""Forward-only DUT models for waveform simulation; no inverse fitting."""
from dataclasses import dataclass
from typing import Callable
import numpy as np

def _z_series_rlc(p, w):
    R, L, C = p
    return R + 1j * (w * L - 1.0 / (w * C))


def _z_series_rc(p, w):
    R, C = p
    return R - 1j / (w * C)


def _z_series_rl(p, w):
    R, L = p
    return R + 1j * w * L


def _z_parallel_rlc(p, w):
    R, L, C = p
    Y = 1.0 / R + 1j * (w * C - 1.0 / (w * L))
    return 1.0 / Y


def _z_parallel_rc(p, w):
    R, C = p
    Y = 1.0 / R + 1j * w * C
    return 1.0 / Y


def _z_parallel_rl(p, w):
    R, L = p
    Y = 1.0 / R - 1j / (w * L)        # 1/(jwL) = -j/(wL)
    return 1.0 / Y


def _z_r_plus_lc_parallel(p, w):
    """R + L∥C : inductor with parallel self-capacitance (SRF model)."""
    R, L, C = p
    Y = 1.0 / (w * L * 1j) + w * C * 1j
    return R + 1.0 / Y


def _z_rs_rp_c(p, w):
    """Rs + Rp∥C : electrolytic capacitor / electrochemical double layer."""
    Rs, Rp, C = p
    return Rs + 1.0 / (1.0 / Rp + 1j * w * C)


@dataclass
class ModelDef:
    name: str
    params: list[str]
    func: Callable
    label: str
    tex: str          # LaTeX impedance expression for the frontend


MODELS: dict[str, ModelDef] = {
    "series_RLC": ModelDef("series_RLC", ["R", "L", "C"], _z_series_rlc, "串联 RLC",
                           r"Z = R + j\omega L + \tfrac{1}{j\omega C}"),
    "series_RC": ModelDef("series_RC", ["R", "C"], _z_series_rc, "串联 RC",
                          r"Z = R + \tfrac{1}{j\omega C}"),
    "series_RL": ModelDef("series_RL", ["R", "L"], _z_series_rl, "串联 RL",
                          r"Z = R + j\omega L"),
    "parallel_RLC": ModelDef("parallel_RLC", ["R", "L", "C"], _z_parallel_rlc, "并联 RLC",
                             r"Z = \bigl(\tfrac{1}{R} + \tfrac{1}{j\omega L} + j\omega C\bigr)^{-1}"),
    "parallel_RC": ModelDef("parallel_RC", ["R", "C"], _z_parallel_rc, "并联 RC",
                            r"Z = \bigl(\tfrac{1}{R} + j\omega C\bigr)^{-1}"),
    "parallel_RL": ModelDef("parallel_RL", ["R", "L"], _z_parallel_rl, "并联 RL",
                            r"Z = \bigl(\tfrac{1}{R} + \tfrac{1}{j\omega L}\bigr)^{-1}"),
    "R_LC_parallel": ModelDef("R_LC_parallel", ["R", "L", "C"], _z_r_plus_lc_parallel,
                              "R + L∥C（电感 SRF）",
                              r"Z = R + \bigl(\tfrac{1}{j\omega L} + j\omega C\bigr)^{-1}"),
    "Rs_Rp_C": ModelDef("Rs_Rp_C", ["Rs", "Rp", "C"], _z_rs_rp_c,
                        "Rs + Rp∥C（电解电容）",
                        r"Z = R_s + \bigl(\tfrac{1}{R_p} + j\omega C\bigr)^{-1}"),
}

