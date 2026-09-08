# LCRTheory.md

## A Unified Theory, Algorithms, Identifiability, and Reliability Framework for Single-Port RLC Network Reconstruction

**Normative technical paper / first draft**  
**Audit baseline:** `invincible-summer/LCR-Analyzer-WebSite`, branch `main`, commit `c79bec8ddef163c6ab416e545e3cdc8b90b7a572` (`try to perfect LCR Algorithm`, 2026-09-08)  
**Language order:** Part I English; Part II Chinese translation  
**Scope:** This document audits the complete *mathematical and algorithmic path* used by the project: waveform-to-impedance measurement, common data contracts, the C++17 Try1/Try2/Try3 engines, graph reduction/enumeration, nodal solvers, nonlinear fitting, model selection, robustification, equivalence clustering, C++/WASM fitting contracts, and the algorithm validation gates. UI layout/CSS and unrelated web presentation code are not treated as mathematical sources.

---

> **Rendered edition note.** Mathematical expressions are typeset in the PDF and represented as MathML in the HTML edition. Numbered citations are cross-linked to the corresponding bibliography; in the HTML edition, hovering over a citation displays the bibliographic annotation.
>
> **渲染版说明。** PDF 中数学公式采用正式数学排版，HTML 中采用 MathML。正文编号引用均与对应参考文献建立交叉链接；HTML 中悬停引用编号可显示文献注释。

---


# Part I — English

## Abstract

This work formalizes the LCR Analyzer reconstruction problem as inference of a passive, linear, time-invariant, lumped two-terminal multigraph from discrete complex driving-point impedance measurements. The network contains ideal resistors, ideal capacitors, and physical inductors represented by an ideal inductance in series with a winding DC resistance. Three inverse problems are distinguished by prior information: **Try2**, in which the complete multiset of components and their nominal/exact values is known but topology is unknown; **Try3**, in which graph topology and edge types are known but values are unknown; and **Try1**, in which topology, types, values, and generally the model order are unknown. The three problems are unified by a single graph representation, nodal forward model, residual/noise model, equivalence relation, and identifiability framework.

The principal theoretical results are: (i) a nodal-admittance representation gives the exact port impedance as \(Z_{01}(s)=b^T Y(s)^{-1}b\); (ii) its parameter sensitivities admit the exact adjoint form \(dZ=-v^T(dY)v\), yielding the analytic Jacobian already implemented by Try3; (iii) finite passive RLC driving-point impedances are positive-real rational functions, but positive-real realizability does **not** imply that an arbitrary realization belongs to the series-parallel hypothesis class used by current Try1; (iv) one-port measurements identify an input-output behavior, not in general a unique internal physical topology; (v) Try2 can possess a conditional global-optimality theorem only when every admissible canonical candidate is evaluated or removed by a proven-safe rule; its current probe-frequency funnel is a speed heuristic and therefore invalidates an unconditional exhaustive-search claim; (vi) Try3's full-column-rank real Jacobian is a sufficient *local* identifiability condition under regularity assumptions, while rank deficiency at a single point is a numerical warning rather than, by itself, a universal proof of global non-identifiability; and (vii) Try1 is correctly described as model discovery over a declared normalized series-parallel hypothesis family assisted by rational fitting and synthesis, not as a globally complete search over all passive RLC graphs.

The paper distinguishes theorem-level facts from assumptions, heuristics, and empirical acceptance thresholds; audits the current implementation against these statements; and defines a final project architecture and verification protocol intended to make future claims reproducible and defensible.

---

## 1. Scope, terminology, and claim hierarchy

A recurring source of error in inverse-circuit software is mixing four logically different statements. This project shall use the following labels in code comments, design documents, test names, and scientific reports:

- **Theorem / Proposition** — follows mathematically from explicitly stated assumptions.
- **Assumption** — defines the physical/statistical hypothesis class and must be testable or stated as a limitation.
- **Heuristic** — improves speed or empirical recovery but may reject or reorder the true model; it is never used to prove completeness.
- **Validation criterion** — an empirical threshold calibrated by simulations/experiments; passing it supports engineering reliability but does not convert it into a theorem.

Examples in the current code are important. Kirchhoff nodal analysis, exact series/parallel reductions, the analytic nodal sensitivity formula, and exhaustive enumeration of a finite canonical candidate set can support mathematical claims. In contrast, Try1's asymptotic F2 decision thresholds, the `rho` regime gate, the 5-sigma IRLS cutoff, Try2's probe funnel, Try3's `weak < 0.1` elasticity rule, and a Jacobian condition-number warning at \(10^4\) are calibrated engineering rules.

### 1.1 Audited implementation sources

The normative code paths at the audit commit include:

- Measurement: `backend/app/dsp/sine_fit.py`, `impedance.py`, `calibration.py`.
- Common contracts: `AlgorithmLcr/INPUT_FORMAT.md`, `OUTPUT_FORMAT.md`, root `DESIGN.md`.
- Try1 C++: `cppversion/src/{circuits,library,pruning,fit_engine_a,fit_engine_b,selector,identify,linalg,adjacency,...}.cpp`.
- Try2 C++: `cppversion/src/{components,graph,enumerate,filters,nodal,metric,selector,identify,...}.cpp` and its C++ test suites.
- Try3 C++: `cppversion/src/{graph,nodal,fit,linalg,metric,adjacency,...}.cpp`.
- Integration and regression: `frontend/wasm`, `AlgorithmLcr/OPTIMIZATION_LOG.md`, `bench/{real4,suite,suite2,realfam}.py`, native C++ tests, glue tests, and WASM smoke tests.

This paper is therefore a theory of the actual project, not a restatement of an idealized algorithm that the repository does not implement.

---

## 2. Physical and mathematical model

### 2.1 Terminal multigraph

A candidate network is

\[
\mathcal N=(G,\tau,\theta),\qquad G=(V,E),
\]

where \(G\) is a finite undirected loop-free multigraph. Parallel edges are allowed. Vertices \(0\) and \(1\) are distinguished terminals. Each edge \(e=(u_e,v_e)\) carries a type

\[
\tau_e\in\{R,C,L\}.
\]

R and C have one physical parameter; an L device has two:

\[
R_e>0,\qquad C_e>0,\qquad L_e>0,\quad R_{d,e}\ge 0.
\]

A physical inductor is one device with

\[
Z_{L,e}(s)=R_{d,e}+sL_e.
\]

The present Try1 and Try3 log-domain implementations use strictly positive lower bounds for \(R_d\), while Try2 accepts \(R_d=0\). **Normative rule:** the mathematical model allows \(R_d=0\). A zero DCR shall be represented as a true boundary/fixed-zero case rather than being excluded by the theory merely because `log10(0)` is undefined.

### 2.2 Primitive impedances and admittances

For \(s\in\mathbb C\),

\[
Z_R=R,\qquad Y_R=R^{-1},
\]

\[
Z_C=(sC)^{-1},\qquad Y_C=sC,
\]

\[
Z_L=R_d+sL,\qquad Y_L=(R_d+sL)^{-1}.
\]

Measurements use \(s=j\omega=j2\pi f\).

### 2.3 Project assumptions

The core inference theory assumes:

1. lumped, linear, time-invariant behavior over the measurement band;
2. passive R/C/(L+DCR) elements with no controlled sources, mutual inductance, hysteresis, saturation, distributed transmission-line behavior, or time variation;
3. reciprocal two-terminal behavior, so terminal interchange does not change driving-point impedance;
4. the measured sweep is sufficiently close to small-signal steady-state behavior that a frequency response exists;
5. the candidate hypothesis family is explicitly declared for each Try.

Violation of these assumptions is a **model mismatch**, not an optimizer failure.

---

## 3. Measurement from sampled voltage and current

### 3.1 Known-frequency three-parameter sine fit

At each excitation frequency \(\omega\), the current backend fits each measured channel to

\[
x_n=a\sin(\omega t_n)+b\cos(\omega t_n)+c+\epsilon_n.
\]

With design matrix

\[
X=\begin{bmatrix}
\sin\omega t_1 & \cos\omega t_1 & 1\\
\vdots&\vdots&\vdots\\
\sin\omega t_N & \cos\omega t_N & 1
\end{bmatrix},
\]

the least-squares estimate is

\[
\hat\beta=(\hat a,\hat b,\hat c)^T=X^+x.
\]

This is the standard known-frequency three-parameter sine fit described by IEEE 1057 [\[1\]](#ref-en-1 "IEEE Std 1057-2017, IEEE Standard for Digitizing Waveform Recorders, Annex A: three-parameter known-frequency sine fitting."). The code convention

\[
A=\sqrt{a^2+b^2},\qquad \phi=\operatorname{atan2}(b,a)
\]

corresponds to

\[
A\sin(\omega t+\phi)=A\cos\phi\sin\omega t+A\sin\phi\cos\omega t.
\]

Since voltage and current use the same convention,

\[
|\hat Z|=\frac{A_V}{A_I},\qquad
\angle\hat Z=\phi_V-\phi_I,
\]

and

\[
\hat Z=|\hat Z|e^{j(\phi_V-\phi_I)}.
\]

**Proposition 3.1.** Under independent Gaussian additive sample noise, known \(\omega\), and a full-rank design matrix, ordinary least squares is the maximum-likelihood estimator of \((a,b,c)\).

The phrase “known frequency” must be interpreted physically: the frequency used in the basis must be consistent with the actual excitation and sample clock. Oscillator or timestamp error creates deterministic phase/amplitude bias; such error is not captured by residual AWGN alone.

### 3.2 Correct covariance propagation

The current `impedance.py` uses the useful approximation

\[
\frac{\sigma_{|Z|}}{|Z|}\approx\sigma_\phi\approx
\sqrt{\left(\frac{s_V}{A_V}\right)^2+
      \left(\frac{s_I}{A_I}\right)^2}\sqrt{\frac{2}{N}},
\]

where \(s_V,s_I\) are residual RMS values. This is approximately correct when sampled sine/cosine columns are nearly orthogonal, coverage is adequate, and channel noise is homoscedastic.

For a paper-grade uncertainty model, the project should instead use

\[
\operatorname{Cov}(\hat\beta)=\hat\sigma_x^2(X^TX)^{-1}
\]

(or the appropriate weighted/generalized covariance), then propagate it to \((A,\phi)\) by the delta method. For

\[
g_A(a,b)=\sqrt{a^2+b^2},\qquad
 g_\phi(a,b)=\operatorname{atan2}(b,a),
\]

\[
\operatorname{Cov}(A,\phi)
\approx J_g\operatorname{Cov}(a,b)J_g^T.
\]

Voltage/current cross-channel covariance should be included if the analog front-end or ADC produces correlated errors. The resulting covariance of \((\Re Z,\Im Z)\) is the statistically preferred input to the inverse algorithms.

### 3.3 Calibration is presently provisional

`backend/app/dsp/calibration.py` explicitly describes itself as functional scaffolding. Its sequence “subtract short impedance, remove open shunt admittance, scale by load” is not a complete claim of traceable one-port VNA error correction. A conventional one-port OSL reflection calibration estimates three systematic terms—directivity, source match, and reflection tracking [\[17\]](#ref-en-17 "Keysight Technologies, “1-Port Calibration (reflection test)” and “Measurement Errors”: one-port OSL calibration corrects directivity, source match, and reflection tracking errors. Manufacturer technical documentation."). The final measurement paper must therefore distinguish:

- raw V/I ratio estimation;
- fixture/analog-front-end calibration appropriate to this particular impedance instrument;
- VNA-style OSL reflection calibration, if a reflection-coefficient architecture is actually used.

No topology-identification algorithm can compensate reliably for an unmodeled frequency-dependent fixture error; such error becomes “systematics” and can make a larger false circuit fit better than the physical DUT.

---

## 4. Unified forward network equation

### 4.1 Incidence formulation

Choose terminal 0 as reference. Let \(A\in\mathbb R^{(|V|-1)\times |E|}\) be the reduced oriented incidence matrix; edge orientation is arbitrary. Let

\[
D_y(s,\theta)=\operatorname{diag}(y_1,\ldots,y_E).
\]

The reduced nodal admittance matrix is

\[
Y(s,\theta)=A D_y(s,\theta)A^T.
\]

Let \(b\) inject \(+1\) A at terminal 1 relative to grounded terminal 0. Nodal KCL is

\[
Yv=b.
\]

When \(Y\) is nonsingular,

\[
v=Y^{-1}b,
\]

and the unit-current port voltage is

\[
\boxed{Z_{01}(s;\theta)=b^TY^{-1}b.}
\]

This is exactly the quantity computed by the Try2 and Try3 nodal solvers, modulo their concrete grounding/index conventions. It is a specialized nodal-analysis form of the classical circuit formulations surveyed by Ho, Ruehli, and Brennan [\[2\]](#ref-en-2 "C.-W. Ho, A. E. Ruehli, and P. A. Brennan, “The modified nodal approach to network analysis,” IEEE Transactions on Circuits and Systems, 22(6), 504–509, 1975. DOI:").

### 4.2 Proof of the port equation

Each column \(a_e\) of \(A\) maps node potentials to the signed edge voltage \(a_e^Tv\). Ohm's law in admittance form gives edge current \(i_e=y_e a_e^Tv\). KCL then gives

\[
A i=A D_yA^Tv=b.
\]

Grounding removes the gauge degree of freedom. With unit port current, terminal-1 voltage relative to terminal 0 equals impedance, so \(Z=b^Tv=b^TY^{-1}b\). QED.

### 4.3 Exact analytic sensitivity theorem

Differentiate

\[
Z=b^TY^{-1}b,
\]

using \(dY^{-1}=-Y^{-1}(dY)Y^{-1}\):

\[
dZ=-b^TY^{-1}(dY)Y^{-1}b=-v^T(dY)v.
\]

Since

\[
dY=\sum_e a_e a_e^T\,dy_e,
\]

we obtain

\[
\boxed{dZ=-\sum_e (a_e^Tv)^2\,dy_e.}
\]

For one edge,

\[
\boxed{\frac{\partial Z}{\partial q_e}
=-\frac{\partial y_e}{\partial q_e}(v_{u_e}-v_{v_e})^2.}
\]

This is precisely the central formula implemented in Try3 `nodal.cpp`.

### 4.4 Log-parameter derivatives

Let \(\theta=\log_{10}q\), so \(dq/d\theta=(\ln10)q\).

For R:

\[
y=R^{-1},\qquad
\frac{\partial y}{\partial\log_{10}R}=-(\ln10)y.
\]

For C:

\[
y=sC,\qquad
\frac{\partial y}{\partial\log_{10}C}=(\ln10)y.
\]

For L+DCR with \(z=R_d+sL\), \(y=z^{-1}\):

\[
\frac{\partial y}{\partial\log_{10}L}
=-(\ln10)Ls\,y^2,
\]

\[
\frac{\partial y}{\partial\log_{10}R_d}
=-(\ln10)R_d\,y^2.
\]

These derivatives should be the **single shared Jacobian source** for Try3, Try2 tolerance refinement, and any graph-based Try1/Try2.5 optimizer.

### 4.5 Elasticity

The dimensionless sensitivity used by Try3 is

\[
E_{q,k}=\frac{\partial\ln Z_k}{\partial\ln q}
=\frac{1}{Z_k\ln10}\frac{\partial Z_k}{\partial\log_{10}q}.
\]

Small \(|E|\) means the parameter has weak influence in the measured band. A threshold such as \(\max_k|E_{q,k}|<0.1\) is a useful engineering flag, not a universal identifiability theorem.

---

## 5. Passivity, rationality, synthesis, and what they do not prove

### 5.1 Positive-real driving-point impedance

A finite passive RLC one-port has a real-rational driving-point impedance that is positive real (PR): under standard definitions,

\[
\Re s>0\quad\Longrightarrow\quad \Re Z(s)\ge0,
\]

with the usual restrictions on imaginary-axis poles. This is a classical network-synthesis result associated with Brune [\[3\]](#ref-en-3 "O. Brune, “Synthesis of a Finite Two-terminal Network whose Driving-point Impedance is a Prescribed Function of Frequency,” Journal of Mathematics and Physics, 10, 191–236, 1931. DOI:"). Consequently, where \(Z(j\omega)\) is finite and regular,

\[
\Re Z(j\omega)\ge0.
\]

This provides a valuable property test for synthetic data, calibration, and numerical solvers.

### 5.2 Rational order

With \(n_E=n_L+n_C\) energy-storage devices, the McMillan degree satisfies

\[
n_{\mathrm{McM}}\le n_E,
\]

and may be strictly smaller because of topology, cancellations, unobservable modes, or exact degeneracies.

A common but unsafe shortcut is to say “an order-\(n\) rational function always has \(2n+2\) identifiable real degrees of freedom.” The actual count depends on numerator and denominator degrees and normalization. More importantly, a count of equations is only necessary. Unique local recovery requires an appropriate rank condition; global uniqueness requires stronger conditions.

### 5.3 Synthesis existence is not Try1 topology completeness

Foster and Cauer constructions provide important canonical realization families. Brune and Bott–Duffin theory establishes broad passive realization results; Bott and Duffin famously provided transformerless impedance synthesis for PR functions [\[4\]](#ref-en-4 "R. J. Duffin and R. Bott, “Impedance synthesis without use of transformers,” Journal of Applied Physics, 20(8), 816, 1949. DOI:"). But the logical implication

> “every PR function has an RLC realization” ⇒ “every such function has a realization in the project's bounded normalized series-parallel tree library”

is false.

Some passive functions require, or admit more economical realizations with, bridge/non-series-parallel structures. Therefore current Try1's series-parallel library has a **declared hypothesis-space guarantee only**. Try2, whose graph enumerator includes bridges and multiedges, is the project's current arbitrary-small-graph topology engine.

### 5.4 Rational fitting is not automatically passive fitting

Try1 Engine B implements a Sanathanan–Koerner/vector-fitting-style pole relocation and residue refit. Vector Fitting is a well-established rational approximation technique for frequency-domain responses [\[9\]](#ref-en-9 "B. Gustavsen and A. Semlyen, “Rational approximation of frequency domain responses by vector fitting,” IEEE Transactions on Power Delivery, 14(3), 1052–1061, 1999. DOI:"). A stable-pole rational approximation, however, is not automatically positive real. Stability and passivity are distinct constraints. Consequently Engine B should be described as:

1. an order/pole/feature estimator;
2. a source of synthesis candidates and optimizer starts;
3. an independent behavioral cross-check;

not as a proof that the fitted rational model is a physically passive RLC network unless a PR/passivity test (and, if necessary, enforcement procedure) succeeds.

---

## 6. Statistical objective and measurement weighting

Let measurements be

\[
\mathcal D=\{(f_k,\hat Z_k)\}_{k=1}^M,
\qquad
\hat Z_k=Z_\star(j\omega_k)+\varepsilon_k.
\]

### 6.1 Current relative complex residual

The C++ engines primarily use

\[
w_k=\frac1{|\hat Z_k|},
\]

and stack

\[
r_{2k}=w_k\Re(\hat Z_k-Z_k),\qquad
r_{2k+1}=w_k\Im(\hat Z_k-Z_k).
\]

Thus

\[
J=\|r\|_2^2=\sum_k\frac{|\hat Z_k-Z_k|^2}{|\hat Z_k|^2}.
\]

This is reasonable when approximately constant relative, isotropic complex error dominates.

### 6.2 Normative generalized least squares

If the measurement path can supply a covariance matrix \(\Sigma_k\) for \((\Re Z_k,\Im Z_k)\), the preferred residual is whitened:

\[
r_k=\Sigma_k^{-1/2}
\begin{bmatrix}
\Re(\hat Z_k-Z_k)\\
\Im(\hat Z_k-Z_k)
\end{bmatrix}.
\]

This connects the waveform estimator, calibration uncertainty, and inverse model in a statistically coherent way.

The raw weight \(1/|Z|\) also requires protection near a zero-impedance resonance. The final implementation should use instrument covariance or, at minimum,

\[
w_k=1/\max(|\hat Z_k|,Z_{\mathrm{floor},k}).
\]

### 6.3 Robust contamination handling

Current Try1 and other recent paths implement a custom IRLS rescue: estimate robust residual scale, identify gross outliers, and downweight them, with guards requiring an inlier majority. This is consistent with the robust-estimation motivation of Huber [\[15\]](#ref-en-15 "P. J. Huber, “Robust Estimation of a Location Parameter,” The Annals of Mathematical Statistics, 35(1), 73–101, 1964. DOI:"), but it is **not exactly Huber's loss** and should not be called a theorem-derived Huber estimator. Report both robust-fit diagnostics and unmodified-weight metrics so candidate comparisons remain interpretable.

### 6.4 AIC and AICc

For comparable likelihood models, AIC/AICc can penalize unnecessary continuous parameters [13,14]. The project's formula is structurally the usual small-sample correction,

\[
\mathrm{AICc}=\mathrm{AIC}+\frac{2k(k+1)}{n-k-1}.
\]

**Normative restriction:** AICc is not valid when \(n\le k+1\). Current code clamps the denominator to at least 1; this is a numerical convenience, not the standard statistical criterion. Future code should report AICc as unavailable/infinite outside its domain rather than silently changing the formula.

For exact-value Try2 all admissible topologies have no fitted continuous parameters; if all have the same known component multiset, ranking by the common residual objective is sufficient. AICc becomes relevant only when parameters are refined or model dimensions differ.

---

## 7. Equivalence and identifiability

### 7.1 Four distinct equivalence relations

The project must distinguish:

**Graph-isomorphic equivalence.** Internal node relabeling (and terminal swap for reciprocal one-ports) represents the same labeled-terminal connection structure.

**Component-permutation equivalence.** Components with identical type and value may be exchanged without creating a new candidate. Unequal components may not be exchanged unless an actual graph automorphism maps their placements appropriately.

**Exact electrical equivalence.** Two networks are equivalent if

\[
Z_{\mathcal N_1}(s)\equiv Z_{\mathcal N_2}(s)
\]

as rational functions.

**Observed-band numerical equivalence.** Current selectors compare model curves on a finite expanded frequency grid and cluster when a relative discrepancy lies below a noise-aware tolerance. This is a useful *approximate* equivalence class; it is not a symbolic proof of exact electrical equivalence.

### 7.2 Spanning-tree / two-forest expression

For a connected admittance graph, matrix-tree results [\[7\]](#ref-en-7 "S. Chaiken, “A Combinatorial Proof of the All Minors Matrix Tree Theorem,” SIAM Journal on Algebraic Discrete Methods, 3(3), 319–329, 1982. DOI:") yield a useful structural form for the effective two-terminal impedance:

\[
Z_{01}(s)=
\frac{\displaystyle\sum_{F\in\mathcal F_{01}}
      \prod_{e\in F} y_e(s)}
     {\displaystyle\sum_{T\in\mathcal T}
      \prod_{e\in T} y_e(s)},
\]

where \(\mathcal T\) is the set of spanning trees and \(\mathcal F_{01}\) is the set of spanning two-forests separating terminals 0 and 1. This formula explains why internal graph structure can be electrically indistinguishable.

Whitney's 2-isomorphism theory [\[8\]](#ref-en-8 "H. Whitney, “2-Isomorphic Graphs,” American Journal of Mathematics, 55(1), 245–254, 1933. DOI:") concerns preservation of cycle-matroid structure. **Important correction to an over-broad statement in older Try2 prose:** ordinary graph 2-isomorphism preserves spanning-tree structure, but terminal driving-point impedance also depends on the *terminal-separating two-forest* polynomial. Therefore “2-isomorphic ⇒ same terminal impedance” must be qualified by terminal preservation / preservation of the relevant two-forest data. The project shall not use generic Whitney 2-isomorphism alone as an unconditional impedance-equivalence theorem.

### 7.3 Topology is generally not globally identifiable from one port

Even knowing \(Z(s)\) exactly does not generally reveal one unique physical realization. Series/parallel collapses, network transformations, canonical synthesis alternatives, label symmetries, and parameter coincidences create equivalent realizations. Finite noisy samples contain strictly less information than exact \(Z(s)\).

Hence the scientifically correct output is normally:

\[
\text{ranked behavioral/electrical equivalence classes}+
\text{diagnostics},
\]

not the assertion “this is the unique physical internal circuit.”

### 7.4 Parameter identifiability

For fixed topology, define the real measurement map

\[
h(\theta)=
\begin{bmatrix}
\Re Z(j\omega_1;\theta) & \Im Z(j\omega_1;\theta)&\cdots
\end{bmatrix}^T\in\mathbb R^{2M}.
\]

Its Jacobian is \(J_h\in\mathbb R^{2M\times p}\).

**Proposition 7.1 (local sufficient condition).** If \(h\) is continuously differentiable, \(\theta_\star\) is an interior point, and \(J_h(\theta_\star)\) has full column rank \(p\), then a suitable local coordinate projection is locally invertible; hence parameters are locally identifiable from the sampled response in a neighborhood of \(\theta_\star\).

This is consistent with classical rank/information-matrix identifiability criteria [11,12].

**Caution.** Rank deficiency at one isolated point does not universally prove a global continuum of exactly equivalent parameters; nonlinear injective maps can have a singular derivative at exceptional points. Persistent/generic rank deficiency, a symbolic invariant, or an explicit parameter-equivalence construction provides stronger structural non-identifiability evidence. The SVD rank reported by Try3 should therefore be called a *local numerical identifiability diagnostic*.

Under approximately whitened Gaussian residuals and regularity,

\[
\operatorname{Cov}(\hat\theta)\approx\sigma^2(J^TJ)^{-1},
\]

so small singular values directly imply high parameter uncertainty.

---

## 8. Try2 — known component multiset, unknown topology

### 8.1 Exact mathematical problem

Let the complete component multiset \(\mathcal C\) be known, including values. Define \(\mathcal G(\mathcal C)\) as the declared set of active connected terminal multigraphs using exactly those components. Then

\[
G^*=\arg\min_{G\in\mathcal G(\mathcal C)} J(G).
\]

There is no continuous fitting in **Try2-Exact**.

Current code also provides bounded value refinement around nominal values. That is useful, but it changes the mathematical problem. The final API should name the modes explicitly:

- **Try2-Exact:** component values are exact; topology only is unknown.
- **Try2-Tolerance:** values are nominal with bounded uncertainty; topology and small value corrections are unknown.

### 8.2 Completeness of structural enumeration

For \(E\) loop-free edges, every connected multigraph satisfies

\[
2\le |V|\le E+1.
\]

For fixed \(V\), there are

\[
S=\binom V2
\]

possible unordered node-pair slots. An E-edge multigraph is exactly a multiset of size \(E\) chosen from these \(S\) slots. Current `enumerate.cpp` iterates \(V=2,\ldots,E+1\), enumerates all such multiplicity vectors, rejects disconnected/dead structures, canonicalizes node labels, then enumerates component assignments modulo equal-component and graph-automorphism symmetries.

**Theorem 8.1 (conditional enumeration completeness).** If:

1. all slot multisets of size \(E\) are generated for every \(2\le V\le E+1\);
2. only properties proven to preserve or remove port-invisible networks are used before canonicalization;
3. canonicalization removes only terminal-preserving graph-isomorphic duplicates;
4. component-assignment orbit reduction removes only assignments related by valid automorphisms/equal components;

then at least one representative of every admissible active loop-free connected E-edge terminal multigraph with the known component multiset is enumerated.

The C++ tests already provide strong evidence: locked counts, brute-force-min canonicalization tests, automorphism tests, R0 pendant-triangle tests, closed-form nodal cases, independent long-double nodal cross-checks, and small-E assignment checks. A further independent reference enumerator should compare **canonical signature sets**, not only counts, for all feasible \(E\le5\).

### 8.3 R0 dead-zone theorem

Suppose removal of an articulation vertex \(c\) separates a connected component \(P\) containing neither terminal. The subnetwork induced by \(P\cup\{c\}\) is connected to the rest of the circuit at only one boundary potential \(V_c\), contains no independent source, and consists of passive branch relations depending only on voltage differences. For every regular \(s\), setting all nodes in \(P\) to \(V_c\) satisfies its internal equations and produces zero boundary current. Under uniqueness of the regular passive nodal solution, that is the solution; therefore this pendant one-boundary subnetwork contributes zero terminal admittance and may be removed without changing \(Z_{01}(s)\).

At isolated lossless singular frequencies internal undriven modes may make internal voltages nonunique, but the driving-point function is still defined by analytic continuation/regular limiting behavior. Thus R0 is a safe topology reduction for the intended passive network model.

Try2 implements the broader articulation-component test, correctly catching a pendant triangle that iterative degree-one removal would miss.

### 8.4 Canonicalization

Current Try2 searches permutations of internal vertices plus terminal interchange and takes a canonical multiplicity vector. This is conceptually correct for reciprocal one-ports. For larger future graphs, canonical labeling should be verified against a mature implementation such as nauty/Traces; McKay and Piperno describe practical canonical-labeling methods [\[16\]](#ref-en-16 "B. D. McKay and A. Piperno, “Practical graph isomorphism, II,” Journal of Symbolic Computation, 60, 94–112, 2014. DOI:"). A “colored multigraph” encoding must preserve terminal colors and component type/value colors.

### 8.5 Forward evaluation

Each canonical network is evaluated using the common nodal equation. Current tests include RLC parallel closed forms, series chains, a balanced Wheatstone bridge, and an independent long-double stamping implementation. These are strong forward-correctness tests.

### 8.6 The current probe funnel is not a safe exhaustive-search proof

Current `filters.cpp` selects a small set of probe frequencies, computes probe residuals, and keeps candidates under a ratio/minimum-count rule. `identify.cpp` performs full-band evaluation only on survivors.

No general implication exists:

\[
J_{\mathrm{probe}}(G_1)<J_{\mathrm{probe}}(G_2)
\quad\Longrightarrow\quad
J_{\mathrm{full}}(G_1)<J_{\mathrm{full}}(G_2).
\]

Therefore current default Try2 is accurately called **Fast Try2**, not strictly exhaustive Try2.

### 8.7 Strict Try2 and a safe early-rejection optimization

Define a strict mode that evaluates every canonical candidate on all frequencies, or use only mathematically safe lower bounds. Because

\[
J(G)=\sum_{k=1}^M \ell_k(G),\qquad \ell_k\ge0,
\]

a partial sum

\[
J_q(G)=\sum_{k=1}^q\ell_{\pi(k)}(G)
\]

is a lower bound on the final cost. If an incumbent has cost \(J_{\mathrm{best}}\), then

\[
J_q(G)>J_{\mathrm{best}}\quad\Rightarrow\quad G\text{ cannot win}.
\]

This branch-and-bound-style early termination is **safe**. Frequencies may be ordered by expected discrimination to reject poor candidates sooner without changing correctness.

**Theorem 8.2 (conditional global optimum over the declared candidate set).** If enumeration is complete, all pre-evaluation reductions are safe, every surviving candidate is evaluated exactly or safely bounded, and the selector returns the minimum objective (or all ties), then Strict Try2 returns a global minimizer over the declared finite candidate space.

This theorem says nothing about uniqueness of the physical circuit outside that space.

### 8.8 Try2-Tolerance refinement

Current bounded refinement is practically valuable for component tolerance, but its implementation uses forward-difference Jacobians and damped normal equations. The project should replace it with the exact common nodal Jacobian of §4 and the SVD-based augmented LM strategy already present in Try3. That removes duplicate mathematics and improves conditioning.

---

## 9. Try3 — known topology and types, unknown values

### 9.1 Problem

After topology/type input and exact structural reduction, let \(p\) be the number of identifiable group parameters. Solve

\[
\hat\theta=\arg\min_{\theta\in\Theta}J(\theta),
\]

preferably in dimensionless/log coordinates.

### 9.2 Exact reduction rules implemented today

Current `graph.cpp` iterates to a fixed point and performs:

- deletion of self loops;
- deletion of components disconnected from the terminal component, while rejecting an open port;
- iterative degree-one nonterminal dangling-edge deletion;
- parallel merge of R edges: \(R_{eq}^{-1}=\sum_iR_i^{-1}\);
- parallel merge of C edges: \(C_{eq}=\sum_iC_i\);
- series merge at degree-two internal nodes: R sums, L and DCR sum, C obeys reciprocal sum;
- series R + (L+DCR) absorption into the inductor DCR.

Every one of these is an exact electrical identity under its stated structural condition. The expression tree retained by the implementation correctly records which original physical edges contribute to an identifiable aggregate.

**Why parallel L is not generically merged.** With unknown \((L_i,R_{d,i})\),

\[
Y=\frac1{R_{d1}+sL_1}+\frac1{R_{d2}+sL_2}
\]

is generally second order and cannot be represented by one \(L+R_d\) primitive.

### 9.3 Audit gap: Try3 should reuse the full R0 reduction

Try3 currently removes disconnected pieces and iterated degree-one branches, but it does **not** implement Try2's general articulation dead-zone test. A pendant triangle attached to the live network through one cut vertex has no degree-one node and can survive Try3 reduction even though it cannot influence the port.

**Normative change:** apply the same R0 articulation-component removal used by Try2 before Try3 parameterization. This reduces dimension, eliminates guaranteed unobservable parameters, and improves numerical conditioning.

### 9.4 Log parameters and zero DCR

For positive R/L/C values, \(\theta_i=\log_{10}q_i\) is an excellent parameterization: positivity is automatic and multiplicative scales become comparable. DCR requires a boundary-safe policy because \(R_d=0\) is physically/mathematically permitted. Recommended choices are:

1. a discrete `ideal_dcr_zero` state plus positive log-DCR when fitted; or
2. a smooth nonnegative map such as \(R_d=R_s\,\mathrm{softplus}(x)\) with explicitly documented scale; or
3. a bounded direct parameter near zero combined with scaled optimization.

Do not silently reinterpret “zero DCR” as \(10^{-6}\ \Omega\) in theoretical statements.

### 9.5 Nonlinear least squares

Try3's current solver is stronger numerically than Try1/Try2 refinement: it forms an augmented damped least-squares problem

\[
\min_{\Delta}\left\|
\begin{bmatrix}J\\\sqrt\lambda D\end{bmatrix}\Delta+
\begin{bmatrix}r\\0\end{bmatrix}
\right\|_2
\]

and solves it by SVD, rather than explicitly solving \((J^TJ+\lambda D)\Delta=-J^Tr\). This avoids the condition-number squaring inherent in normal equations. The method is in the Levenberg–Marquardt family [\[10\]](#ref-en-10 "D. W. Marquardt, “An Algorithm for Least-Squares Estimation of Nonlinear Parameters,” Journal of the Society for Industrial and Applied Mathematics, 11(2), 431–441, 1963. DOI:").

It is still a **local** nonlinear optimizer. Multi-start, resonance-aware seeding, damping continuation, and rescue restarts improve empirical basin coverage but do not establish global optimality.

### 9.6 Jacobian SVD diagnostics

At the fitted solution compute the whitened/weighted real Jacobian

\[
J\in\mathbb R^{2M\times p},\qquad
J=U\Sigma V^T.
\]

Report at least:

\[
\operatorname{rank}_{tol}(J),\quad
\sigma_{\min},\quad\sigma_{\max},\quad
\kappa_2=\sigma_{\max}/\sigma_{\min}.
\]

A full-rank well-conditioned solution supports local identifiability. `cond > 1e4` is a useful current warning threshold, but it is empirical. Better reporting retains singular values and an approximate parameter covariance/confidence interval so users can see the scale continuously.

### 9.7 Boundary and weak-parameter diagnostics

A parameter at a box bound should be reported as `at_bound`; otherwise the optimizer may appear to have identified a value that is actually censored by prior limits. Weak elasticity should be reported separately from rank deficiency: a parameter can be formally locally identifiable yet practically unestimable under realistic noise.

---

## 10. Try2.5 — the missing integration problem

Before general Try1 is expanded, the project should formalize an intermediate problem:

> component types and counts known, topology unknown, values unknown (or only broadly bounded).

Mathematically,

\[
(G^*,\theta^*)=
\arg\min_{G\in\mathcal G}
\left[\min_{\theta\in\Theta_G}J(G,\theta)\right].
\]

This composes the two clean engines:

1. Try2 enumerates/canonicalizes topology;
2. Try3's common graph optimizer fits values for each topology;
3. candidates are compared using a common likelihood/objective and complexity policy;
4. electrically/numerically equivalent classes are returned.

Try2.5 is an essential integration test because it introduces discrete+continuous uncertainty without yet introducing unknown edge types. A failure here is diagnosable as topology enumeration, numerical fitting, or identifiability; Try1 otherwise mixes all three.

---

## 11. Try1 — unknown order, types, topology, and values

### 11.1 Honest problem statement

The unrestricted target would be

\[
(\hat G,\hat\tau,\hat\theta)
=\arg\min_{G,\tau,\theta}
J(G,\tau,\theta)+\text{model-complexity control}.
\]

Current C++ does **not** enumerate arbitrary graphs. Its primary discrete hypothesis family is a canonical normalized series-parallel (SP) tree grammar, bounded by device count and internal depth, supplemented by Engine B rational fitting/synthesis candidates.

Accordingly, the correct claim is:

> Try1 searches for a parsimonious passive RLC behavioral realization in a declared normalized SP hypothesis family, assisted by rational approximation and canonical synthesis; it does not guarantee recovery of every arbitrary passive RLC topology.

### 11.2 SP grammar and canonicalization

Primitive leaves are R, C, and physical L(DCR). Internal nodes are SER or PAR. The current code:

- flattens nested nodes of the same operator (associativity);
- sorts child canonical strings (commutativity);
- prevents directly reducible duplicate R/C leaves;
- retains parallel L(DCR) leaves because they are not generically reducible;
- absorbs a direct series R sibling into an L's DCR;
- alternates SER/PAR structure through recursive generation;
- generates canonical child multisets by device count under a depth budget.

**Proposition 11.1.** Subject to the precise grammar, reduction rules, maximum device count, and maximum internal depth, the multiset DFS in `library.cpp` enumerates every canonical tree admitted by that grammar and suppresses ordering duplicates.

This is a grammar-completeness statement, not arbitrary-graph completeness.

### 11.3 Important exact-count semantics

Because canonicalization intentionally collapses electrically redundant primitive arrangements, `exactN` cannot in general mean “the number of physical packages on the unknown PCB.” Example:

\[
R_1+R_2\equiv R_{eq}=R_1+R_2.
\]

A one-port response cannot infer whether the equivalent resistance is one package or two series packages. Likewise, a series resistor adjacent to an L can be absorbed into its DCR in the current canonical model.

**Normative rule:** Try1 `exactN` shall be documented as the number of devices in the **canonical irreducible equivalent model** searched by Try1, unless a separate nonminimal physical-BOM hypothesis mode is introduced. Calling it an exact physical component count is scientifically unsafe for electrically reducible arrangements.

### 11.4 Engine A

For each surviving SP tree, Engine A fits log parameters with analytic forward derivatives, bounded LM, heuristic/asymptotic starts, resonance starts, and starts supplied by Engine B. It performs staged multi-start fitting and robust refits of leading candidates.

The analytic tree derivative is correct. For a parallel node,

\[
Z=\left(\sum_c Z_c^{-1}\right)^{-1},
\]

so

\[
dZ=Z^2\sum_c\frac{dZ_c}{Z_c^2},
\]

which matches `circuits.cpp`.

The current Engine A LM forms damped normal equations. **Normative future architecture:** migrate it to the Try3 augmented-SVD LM so all continuous fitting shares one solver and one conditioning policy.

### 11.5 F2/F3 pruning audit

Optimization history already demonstrated a critical scientific lesson: the earlier F3 energy-element lower bound could delete the true tree under noise/systematics and was demoted to a non-destructive scheduling key. That change is correct.

F2 remains destructive and infers high-frequency termination behavior from finite measured endpoint slopes/phases. The *tree asymptotic slope calculation* is mathematical; the inference that the finite top of the measurement band has reached the asymptotic regime is an assumption/heuristic.

Therefore:

- **Fast Try1:** may use F2 destructive pruning for performance, with benchmarked miss rate.
- **Strict-SP Try1:** must disable all heuristics that can delete a legal SP candidate, or replace them with proven certificates.

Only Strict-SP mode can support a finite-hypothesis completeness statement.

### 11.6 Engine B

Engine B fits a rational form conceptually of the type

\[
Z(s)=es+d+\frac{k_0}{s}+\sum_i\frac{r_i}{s-p_i},
\]

using pole relocation and residue refitting. It estimates effective order/pole structure, produces Foster-like candidates when mappings are physically admissible, and injects useful starts into Engine A.

Its role is valuable but should remain logically auxiliary unless PR/passivity is certified. It must not be used to infer that a missing SP topology “cannot exist” merely because one finite noisy rational fit did not expose the expected pole pattern.

### 11.7 Model selection and current systematics regime

Current selector logic estimates a local data noise floor and distinguishes a noise-consistent regime from a systematics-dominated regime. In the latter it prefers the fewest-parameter model inside an empirical adequacy band instead of allowing larger mimic models to win purely on residual reduction. This is a sensible engineering defense against fixture/model mismatch and is supported by the project's real-data benchmarks.

It is not a universal maximum-likelihood theorem. The constants (`1.7`, `2.0`, `rho=1.4`, noise-aware clustering factors, etc.) must remain versioned validation parameters with benchmark provenance.

---

## 12. Numerical reliability requirements

### 12.1 Nodal linear solve status

Current Try2/Try3 custom complex LU checks for zero/nonfinite pivots but provides limited conditioning diagnostics. A scientific solver should return

```text
OK | SINGULAR | ILL_CONDITIONED | NONFINITE | PORT_OPEN
```

plus a scaled backward residual

\[
\rho_{lin}=\frac{\|Yv-b\|}
{\|Y\|\|v\|+\|b\|}
\]

and an estimate of reciprocal condition number. Near resonances, “pivot is nonzero” is not sufficient evidence of numerical reliability.

### 12.2 Do not hide singularity behind a finite sentinel

If a solver replaces a singular response by a huge finite `kBigZ`, the fit layer must also receive a diagnostic flag. Otherwise a numerical failure can masquerade as physical high impedance and affect optimization/ranking.

### 12.3 One optimizer core

The preferred project-wide nonlinear least-squares implementation is:

- analytic Jacobian from the common graph solver when possible;
- residual whitening by measurement covariance;
- bounded/trust-region or LM steps;
- augmented SVD/QR solve, not explicit normal equations;
- explicit convergence/status reporting;
- optional robust M-estimation/IRLS as a separately reported layer.

### 12.4 Determinism

Strict correctness modes should be deterministic given the same inputs/configuration. If randomized multi-start is used, report the seed and retain the best-start diagnostics so benchmark failures are reproducible.

---

## 13. Frequency selection and experiment design

The inequality \(2M\ge p\) is merely a necessary dimensional condition for fitting \(p\) real parameters. What matters is information content. For a candidate topology and covariance-whitened Jacobian,

\[
\mathcal I(\theta)=J^TJ
\]

is a Fisher-information-like local matrix. Good frequency sets avoid nearly collinear sensitivity columns and should cover:

- low/high asymptotic regions when physically reachable;
- expected RC/RL break frequencies;
- resonance and antiresonance neighborhoods;
- regions where competing topologies differ most.

Possible active strategies include maximizing \(\sigma_{\min}(J)\), minimizing condition number, D-optimal criteria \(\det(J^TJ)\), or—during topology discrimination—selecting the next frequency maximizing separation among surviving candidate impedances relative to measurement covariance.

High-Q resonances require special care: a sparse log grid can pass entirely between narrow features. Adaptive local refinement is preferable to assuming a fixed 30-point grid is universally sufficient.

---

## 14. Verification and validation protocol

A theorem does not validate floating-point software, and a benchmark does not prove a theorem. The final project must maintain both.

### 14.1 Forward-model property tests

Required analytical cases include single R, C, L+DCR; series/parallel RC, RL, LC; series RLC; parallel resonators; bridge networks with known symmetry; and exact limiting behavior where defined.

Metamorphic tests shall include:

- internal-node permutation invariance;
- terminal interchange invariance for reciprocal networks;
- arbitrary edge-orientation invariance;
- permutation of identical components;
- exact series/parallel reductions;
- graph-to-output-adjacency round-trip preservation of \(Z\).

### 14.2 Independent solver cross-checks

Production nodal results should be compared on random small networks against an independently written higher-precision or established sparse/dense solver. Current Try2 tests already do an independent long-double MNA-style check; this should become common-core testing.

### 14.3 Enumeration proof tests

For small E, build a deliberately slow reference enumerator. Compare complete canonical signature sets:

\[
S_{prod}=S_{oracle}.
\]

Also verify no duplicate production signatures. Counts alone are insufficient.

### 14.4 Jacobian tests

Compare analytic Jacobians with central finite differences over multiple step sizes and, where feasible, complex-step/automatic differentiation. Test both ordinary points and difficult scale-separated/high-Q cases.

### 14.5 Noise/systematics campaigns

For known synthetic truth, run Monte Carlo across:

- Gaussian noise levels;
- heteroscedastic noise;
- sparse outliers;
- smooth multiplicative/phase systematics;
- component tolerance;
- frequency-grid sparsity;
- high Q;
- near-degenerate and exactly equivalent networks.

Report rank-1 and top-K class recovery, fit calibration, parameter bias/variance, false-certainty rate, runtime, and strict-vs-fast disagreement.

The existing `real4`, `suite`, `suite2`, and `realfam` gates are valuable engineering baselines and should remain versioned. They should be supplemented by the theorem-oriented property tests above.

---

## 15. Reliability claims the project may and may not make

### 15.1 Forward solver

**May claim:** Given a declared R/C/L+DCR graph, finite parameters, and a regular well-conditioned nodal matrix, the common nodal formulation computes the unique linear port impedance to floating-point tolerance; its analytic sensitivity formula follows exactly from matrix differentiation.

### 15.2 Strict Try2

**May claim conditionally:** If the truth belongs to the declared finite graph/component hypothesis set, enumeration/canonicalization are complete, only safe reductions are used, and all candidates are fully evaluated or safely bounded, Strict Try2 returns a global minimum of the declared objective over that set.

**May not claim:** the returned representative is the unique physical topology in nature.

### 15.3 Fast Try2

**May claim:** empirically high recovery under a specified benchmark distribution and configuration.

**May not claim:** unconditional exhaustive/global-optimum topology search while the heuristic probe funnel can discard candidates.

### 15.4 Try3

**May claim:** local numerical parameter identifiability is supported when the fitted solution is interior, the weighted Jacobian has full column rank with adequate singular-value separation, the optimizer converges consistently from independent starts, and residuals are compatible with the measurement model.

**May not claim:** global parameter uniqueness merely because one LM run converged or because one numerical rank test is full.

### 15.5 Try1

**May claim:** best-found parsimonious model/equivalence class within the current SP+synthesis hypothesis process, with empirical validation on declared test distributions.

**May not claim:** exhaustive reconstruction of every arbitrary passive RLC topology.

---

## 16. Normative final software architecture

The three engines should converge on one mathematical core:

```text
lcr_core/
  measurement_model      covariance / weights / calibration metadata
  network_graph          terminal colored multigraph + Edge
  components             R, C, L+DCR primitive laws
  nodal                   Z, analytic Jacobian, solve diagnostics
  residual                GLS / relative fallback / robust layer
  metrics                 RSS, wRMSE, maxRel, likelihood, AIC/AICc validity
  equivalence             graph + numerical/electrical class utilities
  numerics                SVD/QR LM, LU/QR solve diagnostics
```

Then:

```text
Try2-Exact      = canonical graph enumeration + exact core evaluation
Try2-Tolerance  = Try2 enumeration + bounded common optimizer
Try3            = known graph/types + exact reduction + common optimizer
Try2.5          = Try2 topology enumeration + Try3 inner fit
Try1            = order/type/topology hypothesis generation + common optimizer
```

This removes the current duplication in weights, AICc, LU, LM, finite-difference refinement, and diagnostics.

### 16.1 Recommended development order

The scientifically clean order is:

\[
\boxed{
\text{Common Core}\rightarrow
\text{Try2-Exact}\rightarrow
\text{Try3}\rightarrow
\text{Try2.5}\rightarrow
\text{Try1}
}
\]

Each stage adds one new source of uncertainty only after the preceding layer is independently trustworthy.

---

## 17. Current implementation audit: principal corrections required

The following are the most important discrepancies between strong theoretical wording and the audited code, and should be resolved before publication-level claims:

1. **Try2 generation is exhaustive, current default identification is not.** The probe funnel is heuristic. Add `STRICT` and `FAST` modes and report the mode in output.
2. **Try1 is SP-hypothesis discovery, not arbitrary graph recovery.** Do not use general PR synthesis existence as proof of SP completeness.
3. **Try1 F2 remains destructive.** Keep it out of Strict-SP mode unless a safe certificate is proved.
4. **Try3 lacks full R0 articulation dead-zone removal.** Reuse Try2's R0 logic before parameter fitting.
5. **Try1/Try3 numerical DCR model excludes exact zero while theory/Try2 allow it.** Add explicit nonnegative boundary support.
6. **Try1 `exactN` is not generally a physical BOM count** because canonical reductions erase electrically redundant devices. Rename/document its semantics.
7. **Try2 tolerance refinement duplicates a weaker optimizer.** Replace forward differences + normal equations with common analytic nodal Jacobian + SVD-LM.
8. **Try1 Engine A normal equations should migrate to SVD/QR augmented LM.**
9. **Current custom LU lacks sufficient near-singularity diagnostics.** Add backward residual and reciprocal-condition estimation.
10. **Current AICc denominator clamp changes the criterion outside its validity domain.** Mark AICc invalid when \(n\le k+1\).
11. **Current `1/|Z|` weighting needs a near-zero floor or, preferably, measurement covariance.**
12. **Sine-fit uncertainty propagation is approximate.** Use the full LS covariance and delta propagation for a paper-grade uncertainty chain.
13. **OSL calibration is scaffolding.** Do not present it as a finalized metrological error model before hardware/front-end topology is fixed and characterized.
14. **Numerical curve clustering is not symbolic electrical equivalence.** Label it `observed-band equivalence` unless an exact rational/network certificate is available.
15. **Jacobian rank/condition diagnostics are local numerical evidence.** Do not promote a single-point rank result into an unconditional global structural-identifiability theorem.

---

## 18. Recommended result schema and scientific verdicts

Every returned solution should carry enough information to know *why* it is trusted:

```text
fit:
  objective, wrmse, max_relative_error
  noise_model / covariance_source
  robust_fit_used, outlier_count

search:
  hypothesis_family
  mode = STRICT | FAST
  structures_generated
  structures_canonical
  candidates_evaluated
  heuristic_pruning_used
  incumbent_bound_pruning_count

numerics:
  solve_status
  worst_linear_backward_error
  worst_rcond
  optimizer_status, starts, seed

identifiability:
  equivalence_class_id
  best_vs_second_gap
  jac_rank, jac_singular_values, jac_condition
  weak_parameters
  at_bound_parameters
  approximate_covariance/confidence_intervals

verdict:
  IDENTIFIABLE_LOCAL
  AMBIGUOUS_EQUIVALENCE_CLASS
  HYPOTHESIS_LIMITED
  NUMERICALLY_UNSTABLE
  DATA_INSUFFICIENT
  MODEL_MISMATCH_SUSPECTED
```

A numerical parameter vector without these diagnostics is not a complete scientific result.

---

## 19. References

[]{#ref-en-1}**[1]** IEEE Std 1057-2017, *IEEE Standard for Digitizing Waveform Recorders*, Annex A: three-parameter known-frequency sine fitting.

[]{#ref-en-2}**[2]** C.-W. Ho, A. E. Ruehli, and P. A. Brennan, “The modified nodal approach to network analysis,” *IEEE Transactions on Circuits and Systems*, 22(6), 504–509, 1975. DOI: https://doi.org/10.1109/TCS.1975.1084079

[]{#ref-en-3}**[3]** O. Brune, “Synthesis of a Finite Two-terminal Network whose Driving-point Impedance is a Prescribed Function of Frequency,” *Journal of Mathematics and Physics*, 10, 191–236, 1931. DOI: https://doi.org/10.1002/sapm1931101191

[]{#ref-en-4}**[4]** R. J. Duffin and R. Bott, “Impedance synthesis without use of transformers,” *Journal of Applied Physics*, 20(8), 816, 1949. DOI: https://doi.org/10.1063/1.1698532

[]{#ref-en-5}**[5]** R. M. Foster, “A Reactance Theorem,” *Bell System Technical Journal*, 3, 259–267, 1924.

[]{#ref-en-6}**[6]** W. Cauer, classical works on realization of prescribed frequency-dependent impedances and ladder synthesis, 1926–1927; see also the bibliography in Brune [3].

[]{#ref-en-7}**[7]** S. Chaiken, “A Combinatorial Proof of the All Minors Matrix Tree Theorem,” *SIAM Journal on Algebraic Discrete Methods*, 3(3), 319–329, 1982. DOI: https://doi.org/10.1137/0603033

[]{#ref-en-8}**[8]** H. Whitney, “2-Isomorphic Graphs,” *American Journal of Mathematics*, 55(1), 245–254, 1933. DOI: https://doi.org/10.2307/2371127

[]{#ref-en-9}**[9]** B. Gustavsen and A. Semlyen, “Rational approximation of frequency domain responses by vector fitting,” *IEEE Transactions on Power Delivery*, 14(3), 1052–1061, 1999. DOI: https://doi.org/10.1109/61.772353

[]{#ref-en-10}**[10]** D. W. Marquardt, “An Algorithm for Least-Squares Estimation of Nonlinear Parameters,” *Journal of the Society for Industrial and Applied Mathematics*, 11(2), 431–441, 1963. DOI: https://doi.org/10.1137/0111030

[]{#ref-en-11}**[11]** T. J. Rothenberg, “Identification in Parametric Models,” *Econometrica*, 39(3), 577–591, 1971. DOI: https://doi.org/10.2307/1913267

[]{#ref-en-12}**[12]** L. Ljung and T. Glad, “On global identifiability for arbitrary model parametrizations,” *Automatica*, 30(2), 265–276, 1994. DOI: https://doi.org/10.1016/0005-1098(94)90029-9

[]{#ref-en-13}**[13]** H. Akaike, “A new look at the statistical model identification,” *IEEE Transactions on Automatic Control*, 19(6), 716–723, 1974. DOI: https://doi.org/10.1109/TAC.1974.1100705

[]{#ref-en-14}**[14]** C. M. Hurvich and C.-L. Tsai, “Regression and time series model selection in small samples,” *Biometrika*, 76(2), 297–307, 1989. DOI: https://doi.org/10.1093/biomet/76.2.297

[]{#ref-en-15}**[15]** P. J. Huber, “Robust Estimation of a Location Parameter,” *The Annals of Mathematical Statistics*, 35(1), 73–101, 1964. DOI: https://doi.org/10.1214/aoms/1177703732

[]{#ref-en-16}**[16]** B. D. McKay and A. Piperno, “Practical graph isomorphism, II,” *Journal of Symbolic Computation*, 60, 94–112, 2014. DOI: https://doi.org/10.1016/j.jsc.2013.09.003

[]{#ref-en-17}**[17]** Keysight Technologies, “1-Port Calibration (reflection test)” and “Measurement Errors”: one-port OSL calibration corrects directivity, source match, and reflection tracking errors. Manufacturer technical documentation.

---

## Appendix A — Proof sketch of safe partial-cost pruning

Let each loss contribution \(\ell_k(G)\ge0\). For any permutation \(\pi\) and \(q<M\),

\[
J(G)=J_q(G)+\sum_{k=q+1}^M\ell_{\pi(k)}(G)\ge J_q(G).
\]

If a fully evaluated incumbent has \(J_{best}<J_q(G)\), then \(J(G)>J_{best}\). Rejecting \(G\) cannot remove a global minimizer. This remains true under fixed nonnegative robust/GLS weights; if weights are recomputed candidate-dependently during evaluation, the bound must be re-derived for that objective.

## Appendix B — Proof sketch of local Jacobian criterion

If \(J_h(\theta_\star)\) has full column rank \(p\), some \(p\times p\) minor is nonsingular. Select the corresponding \(p\) output coordinates to form \(g:\mathbb R^p\to\mathbb R^p\). By the inverse function theorem, \(g\) has a unique local inverse near \(\theta_\star\); hence equality of the full measurement vector in that neighborhood implies equality of parameters. This proves local, not global, identifiability.

## Appendix C — Why physical component count may be unobservable

If two physical arrangements yield an identical primitive equivalent for all \(s\), no one-port frequency experiment can count the hidden packages. Examples include

\[
R_1+R_2=R_{eq},\qquad
C_1\parallel C_2=C_{eq},
\]

and, under the project's physical inductor primitive,

\[
R+(R_d+sL)=(R+R_d)+sL.
\]

Thus Try1 canonicalization is correctly modeling an *electrically irreducible realization*, but an `exactN` prior must use the same semantics.

---

# Part II — 中文完整翻译

## 摘要

本文将 LCR Analyzer 的网络反演问题统一形式化为：根据离散频率上的复数驱动点阻抗测量，推断一个无源、线性、时不变、集中参数的二端口多重图网络。网络中的基本器件包括理想电阻、理想电容，以及由理想电感与绕组直流电阻串联构成的实际电感。根据先验信息不同，项目包含三个主要逆问题：**Try2**——已知完整器件多重集及其标称值/精确值，仅拓扑未知；**Try3**——已知图拓扑与各边器件类型，仅参数未知；**Try1**——拓扑、器件类型、参数以及通常意义上的模型阶数均未知。本文将三类问题统一到同一个图表示、节点前向模型、残差/噪声模型、等价关系与可辨识性框架中。

本文得到并核对实现的核心结论包括：第一，节点导纳模型可将端口阻抗严格写为

\[
Z_{01}(s)=b^TY(s)^{-1}b;
\]

第二，其参数灵敏度具有精确的伴随形式

\[
dZ=-v^T(dY)v,
\]

并进一步给出当前 Try3 已实际实现的解析 Jacobian；第三，有限无源 RLC 单口的驱动点阻抗属于正实有理函数，但“正实函数存在无源实现”**不能推出**“该实现一定属于当前 Try1 使用的有限深度串并联树假设空间”；第四，单端口阻抗一般辨识的是端口行为，而不是唯一的内部物理接线；第五，仅当 Try2 对所有合法规范候选进行完整评价，或者只使用已证明安全的剪枝规则时，才能建立候选空间内的条件性全局最优定理，当前 probe-frequency funnel 属于加速启发式，因此默认实现不能无条件宣称为严格穷举；第六，在正则条件下，Try3 的实 Jacobian 满列秩可作为局部可辨识的充分条件，而单点秩亏本身只能作为强烈的局部数值警告，不能无条件上升为全局不可辨识证明；第七，Try1 应准确描述为：在声明的规范化串并联假设族中进行模型发现，并由有理拟合和网络综合提供辅助候选/初值，而不是对所有无源 RLC 图进行完备搜索。

本文严格区分“定理/命题”“假设”“启发式”“经验验收阈值”，逐项审计当前代码与理论陈述的对应关系，并给出一个最终项目应采用的统一软件架构和验证协议，使未来算法结论具有可复现、可审计、可证明的边界。

---

## 1. 范围、术语与结论层级

逆电路辨识软件中最常见的理论错误之一，是把以下四类完全不同的陈述混在一起。项目后续所有代码注释、设计文档、测试名称和技术报告应统一使用以下层级：

- **定理 / 命题（Theorem / Proposition）**：在明确列出的假设下可以由数学推导得到。
- **假设（Assumption）**：定义物理或统计模型的适用范围；如果现实不满足，必须明确标注为模型失配。
- **启发式（Heuristic）**：可以改善搜索速度或经验成功率，但可能删掉/重排真实模型；不得用于证明完备性。
- **验证准则（Validation criterion）**：通过仿真或实测校准得到的工程阈值；通过测试支持工程可靠性，但不会自动变成数学定理。

在当前项目中，KCL/节点法、严格串并联等价归约、节点模型解析灵敏度、有限候选集合的严格枚举等可以支撑数学证明。相反，Try1 的 F2 端点趋势阈值、`rho` 分区、5σ IRLS、Try2 的 probe funnel、Try3 的 `weak < 0.1` 和 `cond > 1e4` 都属于经验或数值诊断规则。

### 1.1 本文实际审计的实现范围

审计基线为仓库 `invincible-summer/LCR-Analyzer-WebSite` 的 `main@c79bec8ddef163c6ab416e545e3cdc8b90b7a572`。纳入本文数学审计的完整算法路径包括：

- 测量：`backend/app/dsp/sine_fit.py`、`impedance.py`、`calibration.py`；
- 公共数据契约：`AlgorithmLcr/INPUT_FORMAT.md`、`OUTPUT_FORMAT.md`、根目录 `DESIGN.md`；
- Try1 C++：`cppversion/src/{circuits,library,pruning,fit_engine_a,fit_engine_b,selector,identify,linalg,adjacency,...}.cpp`；
- Try2 C++：`cppversion/src/{components,graph,enumerate,filters,nodal,metric,selector,identify,...}.cpp` 及其 C++ 测试；
- Try3 C++：`cppversion/src/{graph,nodal,fit,linalg,metric,adjacency,...}.cpp`；
- 集成与回归：`frontend/wasm`、`AlgorithmLcr/OPTIMIZATION_LOG.md`、`bench/{real4,suite,suite2,realfam}.py`、原生 C++ 测试、glue test 与 WASM smoke test。

因此本文描述的是当前项目真实实现的理论，而不是脱离代码的理想化算法。Vue 页面布局、CSS 等不影响数学行为的展示层代码不作为理论来源。

---

## 2. 物理与数学网络模型

### 2.1 二端多重图

一个候选网络定义为

\[
\mathcal N=(G,\tau,\theta),\qquad G=(V,E),
\]

其中 \(G\) 为有限无向、无自环的多重图，允许同一节点对之间存在多条边。节点 0 和节点 1 是固定端口。每条边

\[
e=(u_e,v_e)
\]

具有类型

\[
\tau_e\in\{R,C,L\}.
\]

R 与 C 各有一个物理参数；L 器件有两个参数：

\[
R_e>0,\qquad C_e>0,
\]

\[
L_e>0,\qquad R_{d,e}\ge0.
\]

本项目中“电感”始终表示一个物理器件：

\[
Z_{L,e}(s)=R_{d,e}+sL_e.
\]

需要特别强调：当前 Try1/Try3 的 log 参数化代码对 DCR 使用正下界，因此实现上不能精确表示 \(R_d=0\)；Try2 则可以接受 0。**最终规范应允许 \(R_d=0\)**，数值层必须显式处理零边界，而不能因为 `log10(0)` 不存在就修改物理理论。

### 2.2 基本器件阻抗与导纳

对复频率 \(s\)：

\[
Z_R=R,\qquad Y_R=\frac1R;
\]

\[
Z_C=\frac1{sC},\qquad Y_C=sC;
\]

\[
Z_L=R_d+sL,\qquad Y_L=\frac1{R_d+sL}.
\]

实测中

\[
s=j\omega=j2\pi f.
\]

### 2.3 核心适用假设

理论默认：

1. 测量频段内网络为集中参数、线性、时不变系统；
2. 网络仅含无源 R/C/(L+DCR)，不含受控源、互感、磁滞、饱和、传输线或时变元件；
3. 网络互易，因此交换端口 0/1 不改变驱动点阻抗；
4. 扫频对应小信号稳态频率响应；
5. 每个 Try 的候选假设空间必须显式声明。

如果真实 DUT 不满足这些条件，应判定为**模型失配**，不能把误差归咎于优化器。

---

## 3. 从采样电压/电流到阻抗测量

### 3.1 已知频率三参数正弦拟合

在每个激励角频率 \(\omega\) 下，当前后端对每个通道拟合

\[
x_n=a\sin(\omega t_n)+b\cos(\omega t_n)+c+\epsilon_n.
\]

定义

\[
X=\begin{bmatrix}
\sin\omega t_1 & \cos\omega t_1 & 1\\
\vdots&\vdots&\vdots\\
\sin\omega t_N & \cos\omega t_N & 1
\end{bmatrix},
\]

最小二乘估计为

\[
\hat\beta=(\hat a,\hat b,\hat c)^T=X^+x.
\]

这对应 IEEE 1057 的已知频率三参数 sine fit [\[1\]](#ref-zh-1 "IEEE Std 1057-2017, IEEE Standard for Digitizing Waveform Recorders, Annex A: three-parameter known-frequency sine fitting.")。当前代码采用

\[
A=\sqrt{a^2+b^2},\qquad
\phi=\operatorname{atan2}(b,a),
\]

因为

\[
A\sin(\omega t+\phi)
=A\cos\phi\sin\omega t+A\sin\phi\cos\omega t.
\]

电压和电流使用完全相同的相位约定，因此

\[
|\hat Z|=\frac{A_V}{A_I},
\qquad
\angle\hat Z=\phi_V-\phi_I,
\]

\[
\hat Z=|\hat Z|e^{j(\phi_V-\phi_I)}.
\]

**命题 3.1。** 若采样噪声为独立高斯加性噪声，\(\omega\) 已知且设计矩阵满秩，则普通最小二乘是 \((a,b,c)\) 的最大似然估计。

但工程上“频率已知”必须包含时钟一致性：用于基函数的 \(\omega\) 必须与真实激励和采样时间基准一致。如果 ESP32 激励时钟或采样时钟存在比例误差，就会产生系统性幅相偏差，这不是残差 AWGN 能描述的误差。

### 3.2 更严格的不确定度传播

当前 `impedance.py` 使用近似式

\[
\frac{\sigma_{|Z|}}{|Z|}
\approx\sigma_\phi
\approx
\sqrt{\left(\frac{s_V}{A_V}\right)^2+
      \left(\frac{s_I}{A_I}\right)^2}
\sqrt{\frac2N}.
\]

当正弦/余弦列近似正交、覆盖周期充分且通道噪声近似同方差时，这个公式是合理近似。

论文级不确定度链应改为使用完整最小二乘协方差：

\[
\operatorname{Cov}(\hat\beta)
=\hat\sigma_x^2(X^TX)^{-1},
\]

或在加权情形使用相应广义最小二乘协方差。然后通过 delta method 将 \((a,b)\) 的协方差传播到

\[
g_A(a,b)=\sqrt{a^2+b^2},
\qquad
 g_\phi(a,b)=\operatorname{atan2}(b,a),
\]

即

\[
\operatorname{Cov}(A,\phi)
\approx J_g\operatorname{Cov}(a,b)J_g^T.
\]

如果 V/I 两个 ADC 通道共享时钟、模拟前端或参考源而存在相关噪声，还应传播跨通道协方差，最终得到 \((\Re Z,\Im Z)\) 的二维协方差矩阵，作为后续 GLS 拟合的直接权重来源。

### 3.3 当前校准层属于过渡实现

`backend/app/dsp/calibration.py` 已在源码中明确标注为 scaffolding。目前采取“短路串联误差扣除 → 开路并联导纳扣除 → 负载复增益缩放”的实用处理，但不能直接宣称为完整、可溯源的一端口 VNA 误差模型。标准一端口 OSL 反射校准通常估计并消除三个系统误差项：directivity、source match、reflection tracking [\[17\]](#ref-zh-17 "Keysight Technologies, “1-Port Calibration (reflection test)” and “Measurement Errors”: one-port OSL calibration corrects directivity, source match, and reflection tracking errors.")。

最终项目应明确区分：

- 原始 V/I 比值阻抗估计；
- 适配本项目具体模拟前端的 fixture/增益/相位校准；
- 若未来硬件真的采用反射系数架构，再使用严格的一端口 OSL/VNA 三项误差模型。

拓扑算法无法可靠“吸收”未建模的频率相关夹具误差。夹具误差会表现为平滑系统误差，导致参数更多的错误网络反而比真实 DUT 拟合得更好，这正是当前 Try1 selector 需要 systematics regime 的根本原因之一。

---

## 4. 统一节点前向方程

### 4.1 关联矩阵形式

取端口节点 0 为参考地。设

\[
A\in\mathbb R^{(|V|-1)\times|E|}
\]

为去掉参考节点后的有向关联矩阵；每条无向边任意指定一个计算方向即可。定义

\[
D_y(s,\theta)=\operatorname{diag}(y_1,\ldots,y_E).
\]

则约化节点导纳矩阵为

\[
Y(s,\theta)=A D_y(s,\theta)A^T.
\]

令向量 \(b\) 表示从节点 1 向节点 0 注入 1 A 的端口激励，则 KCL 为

\[
Yv=b.
\]

当 \(Y\) 非奇异时

\[
v=Y^{-1}b.
\]

因为激励电流为 1 A，端口电压即输入阻抗：

\[
\boxed{Z_{01}(s;\theta)=b^TY^{-1}b.}
\]

这正是 Try2/Try3 节点求解器所计算的量，仅具体节点编号和接地实现不同。该形式属于经典节点分析/MNA 理论的一个无独立电压源特例 [\[2\]](#ref-zh-2 "C.-W. Ho, A. E. Ruehli, and P. A. Brennan, “The modified nodal approach to network analysis,” IEEE Transactions on Circuits and Systems, 22(6), 504–509, 1975. DOI:")。

### 4.2 端口公式证明

关联矩阵第 \(e\) 列记作 \(a_e\)，则边电压为

\[
a_e^Tv.
\]

边电流为

\[
i_e=y_e a_e^Tv.
\]

KCL 给出

\[
Ai=A D_yA^Tv=b.
\]

接地消除了电位整体平移自由度。在 1 A 激励下，节点 1 相对节点 0 的电压等于端口阻抗，所以

\[
Z=b^Tv=b^TY^{-1}b.
\]

证毕。

### 4.3 精确解析灵敏度定理

对

\[
Z=b^TY^{-1}b
\]

求微分，利用

\[
dY^{-1}=-Y^{-1}(dY)Y^{-1},
\]

得到

\[
dZ=-b^TY^{-1}(dY)Y^{-1}b=-v^T(dY)v.
\]

又因为

\[
dY=\sum_e a_ea_e^Tdy_e,
\]

所以

\[
\boxed{dZ=-\sum_e(a_e^Tv)^2dy_e.}
\]

对于单条边参数 \(q_e\)：

\[
\boxed{
\frac{\partial Z}{\partial q_e}
=-\frac{\partial y_e}{\partial q_e}
(v_{u_e}-v_{v_e})^2.}
\]

这不是近似式，而是严格矩阵微分结果；当前 Try3 `nodal.cpp` 实际实现的正是该公式。

### 4.4 log10 参数导数

定义

\[
\theta=\log_{10}q,
\qquad
\frac{dq}{d\theta}=(\ln10)q.
\]

电阻：

\[
y=R^{-1},\qquad
\frac{\partial y}{\partial\log_{10}R}
=-(\ln10)y.
\]

电容：

\[
y=sC,\qquad
\frac{\partial y}{\partial\log_{10}C}
=(\ln10)y.
\]

电感：令

\[
z=R_d+sL,\qquad y=z^{-1},
\]

则

\[
\frac{\partial y}{\partial\log_{10}L}
=-(\ln10)Ls\,y^2,
\]

\[
\frac{\partial y}{\partial\log_{10}R_d}
=-(\ln10)R_d\,y^2.
\]

**最终架构要求：** Try3、Try2-Tolerance、未来 Try2.5 和所有图形式的 Try1 优化都应共享这一套 Jacobian，而不再分别维护有限差分或树专用的重复数值逻辑。

### 4.5 无量纲弹性

Try3 中使用的无量纲灵敏度可以写为

\[
E_{q,k}=
\frac{\partial\ln Z_k}{\partial\ln q}
=
rac{1}{Z_k\ln10}
\frac{\partial Z_k}{\partial\log_{10}q}.
\]

若整个频带中 \(|E|\) 都很小，说明该参数对现有测量激励不敏感。当前 `0.1` 阈值可以作为工程 weak 参数提示，但它不是普适可辨识定理。

---

## 5. 无源性、正实性、有理性与网络综合边界

### 5.1 正实驱动点阻抗

有限无源 RLC 单端口的驱动点阻抗是实系数有理正实函数。标准意义下，当

\[
\Re s>0
\]

时满足

\[
\Re Z(s)\ge0,
\]

并对虚轴极点满足相应正实条件。这属于 Brune 网络综合理论的经典结果 [\[3\]](#ref-zh-3 "O. Brune, “Synthesis of a Finite Two-terminal Network whose Driving-point Impedance is a Prescribed Function of Frequency,” Journal of Mathematics and Physics, 10, 191–236, 1931. DOI:")。因此在 \(Z(j\omega)\) 有限且正则的频率处，有必要条件

\[
\Re Z(j\omega)\ge0.
\]

该性质应作为测量、校准、合成数据和数值前向求解器的重要 property test。

### 5.2 有理函数阶数

若储能元件总数为

\[
n_E=n_L+n_C,
\]

则端口阻抗的 McMillan degree 满足

\[
n_{\mathrm{McM}}\le n_E.
\]

由于拓扑简并、极零消去、不可观测内部模式或特定参数关系，实际阶数可以更低。

因此不能简单使用“有 n 个储能元件，所以有理函数必有固定 \(2n+2\) 个独立实自由度”的粗略说法。真实自由度取决于分子/分母阶数与归一化方式，而且“方程数量 ≥ 参数数量”只是必要条件，局部唯一性仍需要 Jacobian/信息矩阵秩条件，全局唯一性则需要更强证明。

### 5.3 综合存在性不等于 Try1 拓扑完备性

Foster、Cauer 给出了重要的规范网络综合族。Brune 与 Bott–Duffin 理论覆盖更一般的正实函数无源实现；Bott–Duffin 给出了不使用变压器的阻抗综合结果 [\[4\]](#ref-zh-4 "R. J. Duffin and R. Bott, “Impedance synthesis without use of transformers,” Journal of Applied Physics, 20(8), 816, 1949. DOI:")。

但以下推理是错误的：

> 任意 PR 函数存在某个 RLC 实现  
> ⇒ 任意 PR 函数都一定存在于项目当前有限深度、有限器件数的规范 SP 树库中。

一般无源函数可能需要桥式结构，或者桥式结构能提供更低阶/更自然的实现。因此当前 Try1 的严格能力只能描述为**声明的 SP 假设空间内的搜索能力**。项目中当前真正可以枚举小规模桥式/多重边图的是 Try2。

### 5.4 有理拟合不自动保证无源性

Try1 Engine B 使用 Sanathanan–Koerner / Vector Fitting 风格的极点搬移和留数重拟合。Vector Fitting 是成熟的频域有理逼近方法 [\[9\]](#ref-zh-9 "B. Gustavsen and A. Semlyen, “Rational approximation of frequency domain responses by vector fitting,” IEEE Transactions on Power Delivery, 14(3), 1052–1061, 1999. DOI:")，特别适合谐振丰富的网络响应。

但是：

\[
\text{stable poles}
\not\Rightarrow
\text{positive real / passive}.
\]

稳定性与无源性是两个不同约束。因此 Engine B 最合理的理论角色是：

1. 估计有效阶数、极点和谐振特征；
2. 产生 Foster-like 可实现候选；
3. 为 Engine A 提供高质量初值；
4. 作为独立的端口行为近似。

只有通过正实性/无源性检验后，才能把某个有理模型本身声明为无源网络模型。

---

## 6. 统计目标、权重与鲁棒性

定义测量数据

\[
\mathcal D=\{(f_k,\hat Z_k)\}_{k=1}^M,
\]

\[
\hat Z_k=Z_\star(j\omega_k)+\varepsilon_k.
\]

### 6.1 当前相对复残差

C++ 三引擎主要采用

\[
w_k=\frac1{|\hat Z_k|},
\]

并将复残差拆成实向量：

\[
r_{2k}=w_k\Re(\hat Z_k-Z_k),
\]

\[
r_{2k+1}=w_k\Im(\hat Z_k-Z_k).
\]

总目标函数为

\[
J=\|r\|_2^2
=\sum_k\frac{|\hat Z_k-Z_k|^2}{|\hat Z_k|^2}.
\]

当主要误差近似为各频点相对幅度恒定、复平面近似各向同性噪声时，该目标合理。

### 6.2 最终推荐：广义最小二乘

若测量层能输出 \((\Re Z_k,\Im Z_k)\) 的协方差矩阵 \(\Sigma_k\)，应使用白化残差：

\[
r_k=\Sigma_k^{-1/2}
\begin{bmatrix}
\Re(\hat Z_k-Z_k)\\
\Im(\hat Z_k-Z_k)
\end{bmatrix}.
\]

这样波形估计、不确定度传播和电路反演形成一条统计上一致的链路。

此外，原始

\[
w=1/|Z|
\]

在理想串联谐振、近零阻抗处可能发散。最终实现至少应使用

\[
w_k=\frac1{\max(|\hat Z_k|,Z_{floor,k})},
\]

更理想的方案仍是直接使用仪器协方差。

### 6.3 离群点鲁棒处理

当前 Try1 和近期其它路径加入了定制 IRLS：估计残差尺度，对超过阈值的野点降权，并要求内点占多数才启动 rescue。这种设计与 Huber 鲁棒统计思想 [\[15\]](#ref-zh-15 "P. J. Huber, “Robust Estimation of a Location Parameter,” The Annals of Mathematical Statistics, 35(1), 73–101, 1964. DOI:") 一致，但项目当前权函数并非严格 Huber loss，因此文档应称为“自定义 IRLS/robust reweighting”，而不是声称实现了某个标准 M-estimator 的全部理论。

候选间最终比较应继续报告原始统一权重下的指标，同时单独报告是否进行了 robust refit 与离群点数量。

### 6.4 AIC/AICc

当不同候选共享同一似然假设时，AIC/AICc 可用于抑制无必要参数 [13,14]。标准小样本修正形式为

\[
\mathrm{AICc}=\mathrm{AIC}
+\frac{2k(k+1)}{n-k-1}.
\]

**最终规范：当 \(n\le k+1\) 时，AICc 不应继续使用。** 当前代码把分母最小截成 1，这只是避免数值异常的工程处理，并非标准 AICc。未来应将其标记为 unavailable/∞，而不是静默修改统计准则。

Try2-Exact 中所有候选使用完全相同的已知器件集合且没有连续拟合参数，因此直接比较统一 residual objective 即可；只有进入 Try2-Tolerance 或不同模型维数竞争时，复杂度惩罚才有额外意义。

---

## 7. 等价关系与可辨识性

### 7.1 必须区分的四类等价

**图同构等价。** 内部节点重新编号仍是同一连接结构；对于互易二端网络，交换端口 0/1 也不改变驱动点阻抗。

**器件置换等价。** 类型和值都相同的器件互换不构成新候选；不同参数器件只有在真实图自同构映射下才能视作等价。

**精确电学等价。** 若

\[
Z_{\mathcal N_1}(s)
\equiv
Z_{\mathcal N_2}(s)
\]

作为有理函数恒等，则两个网络在端口上严格不可区分。

**观测频带数值等价。** 当前 selector 在扩展后的有限对数频率网格上比较两个候选曲线，并依据噪声相关阈值聚类。这是非常实用的近似等价类，但不是符号意义上的严格电学恒等证明。

### 7.2 生成树 / 端口分离二森林表达式

由矩阵树定理及 all-minors matrix-tree theorem [\[7\]](#ref-zh-7 "S. Chaiken, “A Combinatorial Proof of the All Minors Matrix Tree Theorem,” SIAM Journal on Algebraic Discrete Methods, 3(3), 319–329, 1982. DOI:")，对于连通导纳图，二端有效阻抗可写为

\[
Z_{01}(s)=
\frac{\displaystyle
\sum_{F\in\mathcal F_{01}}
\prod_{e\in F} y_e(s)}
{\displaystyle
\sum_{T\in\mathcal T}
\prod_{e\in T} y_e(s)},
\]

其中 \(\mathcal T\) 是所有生成树集合，\(\mathcal F_{01}\) 是将端口 0、1 分别置于两个分量中的生成二森林集合。

这个表达式直接说明：端口阻抗依赖的是某些组合图多项式，而不是“内部拓扑字符串”本身，因此不同图可能产生完全相同的端口行为。

Whitney 的 2-isomorphism [\[8\]](#ref-zh-8 "H. Whitney, “2-Isomorphic Graphs,” American Journal of Mathematics, 55(1), 245–254, 1933. DOI:") 主要描述 cycle matroid / 生成树结构保持。旧 Try2 文档中若直接写成

> 任意 Whitney 2-isomorphic 图必然具有完全相同的二端输入阻抗

则表述过强，因为端口阻抗分子还依赖**端口分离二森林**。更严格的充分条件应要求 terminal-preserving，并保持与端口有关的二森林数据。后续文档不得把一般 2-isomorphism 单独当成无条件二端阻抗等价定理。

### 7.3 单端口一般不能唯一确定内部物理拓扑

即使连续频带上精确知道 \(Z(s)\)，内部实现通常也不唯一。原因包括：

- 串并联可约器件；
- 内部节点/器件标签对称；
- Y-Δ 等网络变换或特定参数巧合；
- Foster/Cauer/Brune 等不同综合形式；
- 更一般的图多项式等价。

离散、带噪的有限频点包含的信息更少。

因此最科学的输出形式是

\[
\boxed{
\text{排序后的行为/电学等价类}
+\text{数值与可辨识诊断}}
\]

而不是无条件输出“唯一真实内部电路”。

### 7.4 固定拓扑下的参数可辨识性

固定图结构后，定义实测量映射

\[
h(\theta)=
\begin{bmatrix}
\Re Z(j\omega_1;\theta)\\
\Im Z(j\omega_1;\theta)\\
\vdots
\end{bmatrix}
\in\mathbb R^{2M}.
\]

其 Jacobian 为

\[
J_h\in\mathbb R^{2M\times p}.
\]

**命题 7.1（局部充分条件）。** 若 \(h\) 连续可微，\(\theta_\star\) 位于参数域内部，并且

\[
\operatorname{rank}J_h(\theta_\star)=p,
\]

则存在一组 \(p\) 个输出坐标使对应 \(p\times p\) Jacobian 子矩阵可逆；由逆函数定理，\(\theta_\star\) 邻域内参数可以由测量局部唯一恢复。因此满列秩是局部可辨识的充分条件。这与经典参数模型的 rank / Fisher information 识别理论一致 [11,12]。

**重要限定：** 单个点 Jacobian 秩亏并不总能证明全局存在一条完全等价参数流形。非线性函数可以在孤立点导数为零但仍保持单射。因此 Try3 的数值秩应称为**局部数值可辨识诊断**。若要证明结构性不可辨识，应使用 generic/symbolic rank、邻域持续秩亏或显式构造等价参数变换。

在白化高斯残差及正则条件下，近似参数协方差为

\[
\operatorname{Cov}(\hat\theta)
\approx \sigma^2(J^TJ)^{-1},
\]

因此很小的奇异值会直接导致极大的参数不确定度。

---

## 8. Try2——已知器件多重集，仅拓扑未知

### 8.1 严格问题定义

设完整器件多重集 \(\mathcal C\) 已知，包括每个元件的类型与参数。定义 \(\mathcal G(\mathcal C)\) 为使用这些器件、满足声明活动性条件的连通二端多重图集合，则 Try2-Exact 为

\[
G^*=\arg\min_{G\in\mathcal G(\mathcal C)}J(G).
\]

这里没有连续参数拟合。

当前实现还允许围绕标称值进行有界精调，这在工程上很有价值，但数学问题已经改变。因此最终 API 应正式区分：

- **Try2-Exact**：元件值视为精确，仅拓扑未知；
- **Try2-Tolerance**：元件值为带容差的标称值，拓扑和小范围参数修正均未知。

### 8.2 拓扑枚举的完备性

对无自环且含 \(E\) 条边的连通多重图，有

\[
2\le |V|\le E+1.
\]

固定 \(V\) 时，可能的无向节点对槽位数为

\[
S=\binom V2.
\]

一个 E 边多重图恰好等价于：从这 \(S\) 个槽位中选择大小为 \(E\) 的多重集，即给每个槽位一个非负整数重数，且总和为 \(E\)。

当前 `enumerate.cpp` 对

\[
V=2,3,\ldots,E+1
\]

逐一枚举所有 multiplicity vector，然后执行连通性、R0 死区、规范化，再对器件实例赋值，并利用相同器件置换和图自同构消除重复。

**定理 8.1（条件性枚举完备）。** 若满足：

1. 对每个 \(2\le V\le E+1\) 生成所有大小 E 的节点对多重集；
2. 规范化之前只使用已证明不会删除端口可观测合法网络的规则；
3. canonicalization 只删除 terminal-preserving 图同构重复；
4. 器件 assignment 的 orbit reduction 只删除由真实自同构或完全相同器件置换得到的重复；

则每一个满足假设的活动、连通、无自环 E 边二端多重图至少保留一个代表元。

当前 C++ 测试已经提供很强实现证据：结构数量锁定、canonical=min brute force、automorphism、pendant triangle R0、RLC 闭式节点分析、独立 long-double 节点求解交叉验证以及小 E assignment 检查。下一步应再加入一个刻意低效的独立 oracle，对 \(E\le5\) 比较**完整 canonical signature 集合**，而不仅是数量。

### 8.3 R0 死区定理

若删除割点 \(c\) 后，存在一个连通分量 \(P\) 不含端口 0 和 1，则 \(P\cup\{c\}\) 这个子网络只通过一个边界电位 \(V_c\) 与外界连接，内部没有独立源，所有支路关系只依赖节点电压差。

在任意正则 \(s\) 下，令 \(P\) 内所有节点电位都等于 \(V_c\)，则所有内部边电压和电流均为零，边界净电流也为零。对于正则无源网络的唯一节点解，这就是该挂载子网络的解，因此该部分对端口导纳贡献为零，删除它不改变 \(Z_{01}(s)\)。

如果完全无耗损网络在某个孤立频率存在内部本征模，内部节点电压可能不唯一，但端口驱动点函数仍可由正则频率解析延拓/极限定义。因此在本项目无源模型下，R0 是安全的结构归约。

Try2 当前实现的是完整的割点-分量判定，所以能正确删除“悬挂三角形”；单纯迭代删除度 1 节点做不到这一点。

### 8.4 图规范化

Try2 当前遍历内部节点置换，并允许端口 0/1 对换，然后取最小 multiplicity vector 作为 canonical 形式。对于当前互易二端 RLC 网络，这是合理的。

如果未来 E 增大，建议把成熟 canonical labeling 实现（如 nauty/Traces）作为独立 differential oracle。McKay 与 Piperno 对实用 graph isomorphism/canonical labeling 有系统论述 [\[16\]](#ref-zh-16 "B. D. McKay and A. Piperno, “Practical graph isomorphism, II,” Journal of Symbolic Computation, 60, 94–112, 2014. DOI:")。工程编码时应使用“带颜色的多重图”：端口颜色固定，器件类型和值也必须作为边颜色/标签保留。

### 8.5 前向评价

每个 canonical network 都调用统一节点方程。当前 Try2 已测试：R||C||L(DCR) 闭式、串联链、平衡惠斯通桥以及独立 long-double stamping。这一部分具备较好的前向正确性证据。

### 8.6 当前 probe funnel 不能证明严格穷举全局最优

当前 `filters.cpp` 先挑少量 probe 频点，计算 probe residual，再按 `funnelRatio` 和最少保留数量选 survivor；`identify.cpp` 只对 survivor 进行完整频带计算。

一般情况下并不存在

\[
J_{probe}(G_1)<J_{probe}(G_2)
\Longrightarrow
J_{full}(G_1)<J_{full}(G_2).
\]

因此当前默认实现应称作 **Fast Try2**，不能称为无条件 Strict Exhaustive Try2。

### 8.7 Strict Try2 与可证明安全的提前终止

严格模式可以全频评估每个 canonical candidate，也可以使用严格 lower bound。

若

\[
J(G)=\sum_{k=1}^M\ell_k(G),
\qquad \ell_k\ge0,
\]

则任意已计算前 q 项的部分和

\[
J_q(G)=\sum_{k=1}^q\ell_{\pi(k)}(G)
\]

都满足

\[
J_q(G)\le J(G).
\]

如果当前已有完整候选的最优值 \(J_{best}\)，一旦

\[
J_q(G)>J_{best},
\]

就可以严格推出

\[
J(G)>J_{best},
\]

因此该候选可立即终止。频率顺序 \(\pi\) 可以优先选择候选区分度最大的频点，从而提高速度，但不会破坏正确性。

**定理 8.2（候选空间内条件性全局最优）。** 若：枚举完备、所有前置归约安全、每个候选被完整评价或由合法 lower bound 排除、selector 返回最小值及所有并列等价类，则 Strict Try2 返回声明有限候选空间中目标函数的全局最小值。

该结论仍然不等于“自然界内部物理接线唯一”。

### 8.8 Try2-Tolerance 精调

当前 Try2 bounded refinement 使用 forward-difference Jacobian，并在阻尼法方程上求步长。建议直接换成第 4 节统一节点解析 Jacobian + Try3 的 SVD 增广 LM。这样既消除重复实现，也避免法方程条件数平方问题。

---

## 9. Try3——已知拓扑和器件类型，仅参数未知

### 9.1 数学问题

对输入图先进行严格结构归约。设归约后需要估计的有效参数数量为 p，则

\[
\hat\theta=\arg\min_{\theta\in\Theta}J(\theta).
\]

R/L/C 正参数应优先在 log/dimensionless 坐标中优化。

### 9.2 当前实现的严格归约规则

`graph.cpp` 迭代到固定点，当前包括：

- 删除 self-loop；
- 删除与端口连通分量无关的边；若端口 0/1 不连通则报 open port；
- 迭代删除非端口度 1 dangling branch；
- 同节点对并联 R 合并：
  \[
  R_{eq}^{-1}=\sum_iR_i^{-1};
  \]
- 并联 C 合并：
  \[
  C_{eq}=\sum_iC_i;
  \]
- 内部度 2 节点上的同类串联合并：R 相加、C 倒数和、L 与 DCR 相加；
- 串联 R + L(DCR) 吸收到该电感 DCR 中。

这些规则在声明结构条件下都是精确电学恒等式。当前实现保留 expression tree，把原始边到可辨识 group 的映射记录下来，这是正确设计，因为最终用户真正能够辨识的往往是组合参数，而不是每个物理封装单独值。

### 9.3 为什么并联 L 不能一般合并

两个未知 \((L,R_d)\) 电感并联时

\[
Y=\frac1{R_{d1}+sL_1}
+\frac1{R_{d2}+sL_2},
\]

一般为二阶有理函数，并不等价于单个

\[
\frac1{R_d+sL}.
\]

因此当前 Try3 不强行合并 parallel L 是正确的。

### 9.4 审计发现：Try3 应复用 Try2 完整 R0

Try3 当前只删除 disconnected 和迭代 degree-one dangling，而没有 Try2 的一般割点死区检查。一个通过单一割点挂在主网络上的三角形，其内部节点度数都可以 ≥2，因此不会被当前 degree-one 逻辑删除，但端口上完全不可见。

**最终规范：Try3 参数化前先执行与 Try2 相同的 R0 articulation dead-zone removal。** 这样可以减少必然不可观测参数、降低维数并改善数值条件。

### 9.5 log 参数与零 DCR

对正的 R/L/C，使用

\[
\theta_i=\log_{10}q_i
\]

是很好的参数化：自动满足正性，并将跨多个数量级的乘性变化变成近似同尺度。

DCR 则必须特殊处理，因为理论允许 \(R_d=0\)。可选实现包括：

1. 将“理想零 DCR”设为离散固定状态，只有 \(R_d>0\) 时使用 log；
2. 使用显式非负参数化，如带尺度的 softplus；
3. 在接近零处直接使用有界线性参数，并做尺度归一化。

不能在论文中把物理上的“0 Ω”默默改写成 \(10^{-6}\ \Omega\)。

### 9.6 非线性最小二乘

当前 Try3 的连续优化器在数值结构上比当前 Try1/Try2 refinement 更合理。它求解增广阻尼最小二乘：

\[
\min_\Delta
\left\|
\begin{bmatrix}J\\\sqrt\lambda D\end{bmatrix}
\Delta+
\begin{bmatrix}r\\0\end{bmatrix}
\right\|_2,
\]

并通过 SVD 求解，而不是显式构造

\[
(J^TJ+\lambda D)\Delta=-J^Tr.
\]

这样避免了 normal equations 把条件数平方。算法属于 Levenberg–Marquardt 家族 [\[10\]](#ref-zh-10 "D. W. Marquardt, “An Algorithm for Least-Squares Estimation of Nonlinear Parameters,” Journal of the Society for Industrial and Applied Mathematics, 11(2), 431–441, 1963. DOI:")。

但它仍然是**局部非线性优化器**。多初值、高 Q 谐振初值、damping continuation、rescue restart 都只能提高经验 basin coverage，不能证明全局最优。

### 9.7 Jacobian SVD 诊断

在最终解处计算白化/加权实 Jacobian

\[
J\in\mathbb R^{2M\times p},
\qquad
J=U\Sigma V^T.
\]

至少应报告：

\[
\operatorname{rank}_{tol}(J),
\quad \sigma_{min},
\quad \sigma_{max},
\quad \kappa_2=\frac{\sigma_{max}}{\sigma_{min}}.
\]

满秩且条件良好可以支持局部可辨识结论。当前 `cond > 1e4` 是合理的工程警告，但不是物理定理。最终 UI/输出最好直接显示奇异值和近似参数置信区间，让用户看到连续的“不确定性程度”，而不是只有二元 pass/fail。

### 9.8 边界与弱参数

参数若达到 box bound，应显式标记 `at_bound`；否则用户可能把“被先验截断”的值误认为真正由数据辨识得到。

`weak` 灵敏度应与 rank deficiency 区分：一个参数可能数学上局部可辨识，但在实际噪声水平下几乎无法精确估计。

---

## 10. Try2.5——应正式加入的中间问题

在继续扩展 Try1 之前，应把下列问题正式定义为 Try2.5：

> 已知器件类型和数量，拓扑未知，参数也未知或只给宽范围。

数学上：

\[
(G^*,\theta^*)=
\arg\min_{G\in\mathcal G}
\left[
\min_{\theta\in\Theta_G}J(G,\theta)
\right].
\]

实现可以直接组合已经验证的两个核心：

1. Try2 负责 topology enumeration / canonicalization；
2. Try3 公共 graph optimizer 对每个拓扑拟合参数；
3. 用统一 likelihood/objective 和复杂度规则比较候选；
4. 输出端口电学/数值等价类。

Try2.5 是非常关键的系统集成测试：它首次同时引入离散拓扑和连续参数，但尚未引入“器件类型未知”。失败时仍可明确定位为枚举问题、优化问题或可辨识问题。如果直接从 Try3 跳到 Try1，这三类误差会被混成一个黑箱。

---

## 11. Try1——阶数、类型、拓扑、参数均未知

### 11.1 严格问题与当前实现能力

无限制的理想目标为

\[
(\hat G,\hat\tau,\hat\theta)
=
\arg\min_{G,\tau,\theta}
J(G,\tau,\theta)
+\text{model complexity control}.
\]

但当前 C++ **并没有枚举任意图**。主要离散假设族是规范化串并联树（SP tree），受最大器件数和最大内部深度限制；此外 Engine B 通过有理拟合/综合提供辅助候选。

因此 Try1 最终应采用以下准确表述：

> Try1 在声明的规范化 SP 假设空间中搜索一个简约无源 RLC 端口行为实现，并由有理逼近与规范网络综合辅助产生候选和初值；它不保证恢复任意无源 RLC 内部图。

### 11.2 SP grammar 与 canonicalization

基本叶子为 R、C、L(DCR)，内部节点为 SER 或 PAR。当前代码执行：

- 同类 SER/PAR 嵌套扁平化（结合律）；
- child canonical string 排序（交换律）；
- 去除可以直接等效合并的重复 R/C leaf；
- 保留并联 L(DCR)，因为一般不能合并为一个一阶 L+DCR；
- 串联节点中 R 与 L 相邻时，将 R 吸收到 L 的 DCR；
- 递归生成时让 SER/PAR 层交替；
- 在给定 device count 与 max internal depth 内用 canonical child multiset DFS 枚举。

**命题 11.1。** 在精确声明的 grammar、归约规则、最大器件数和最大深度约束下，`library.cpp` 的 multiset DFS 会枚举该 grammar 所允许的每一个 canonical tree，并通过 canonical ordering 消除 child 排序重复。

这是 **grammar completeness**，不是 arbitrary graph completeness。

### 11.3 `exactN` 的重要语义问题

由于 Try1 canonicalization 主动消除电学冗余，`exactN` 一般不能解释为“未知 PCB 上真实物理封装器件的数量”。例如

\[
R_1+R_2\equiv R_{eq}=R_1+R_2.
\]

任意单端口频率响应都无法区分“两个串联电阻封装”和“一个等值电阻”。类似地，在项目的物理电感模型中

\[
R+(R_d+sL)=(R+R_d)+sL,
\]

串联 R 也可完全吸收到 DCR。

**最终规范：** Try1 的 `exactN` 应明确定义为“Try1 规范不可约等效模型中的器件数”，除非未来额外提供一个允许非最简物理 BOM 的假设模式。把它描述为一般意义上的精确物理元件数是不严谨的，因为数据本身可能根本不可观测这些冗余封装。

### 11.4 Engine A

对每一个保留 SP tree，Engine A 使用：

- log10 参数；
- 解析前向 Jacobian；
- 有边界 LM；
- 渐近/启发式初值；
- 谐振初值；
- Engine B 注入的 Foster 初值；
- 分阶段 multi-start；
- 头部候选 robust refit。

树解析导数是正确的。例如并联节点

\[
Z=\left(\sum_cZ_c^{-1}\right)^{-1},
\]

所以

\[
dZ=Z^2\sum_c\frac{dZ_c}{Z_c^2},
\]

与 `circuits.cpp` 当前实现一致。

当前 Engine A LM 仍显式形成阻尼法方程。最终建议迁移到 Try3 的 augmented-SVD LM，使整个项目只维护一套连续优化器和条件数策略。

### 11.5 F2/F3 剪枝审计

`OPTIMIZATION_LOG.md` 已经留下非常重要的科学证据：旧 F3 “储能元件阶数下界”在噪声/系统误差下会删掉真树，因此已被降级为**非破坏性排序 key**。这个修改是正确的。

F2 仍是 destructive：它根据测量频带高端有限几个点的斜率和相位，推断模型的 \(\omega\to\infty\) 终端趋势。树本身的渐近 slope 推导是数学的；但“当前最高测量频率已经进入真正渐近区”只是工程假设。

所以应明确区分：

- **Fast Try1**：可以开启 F2 destructive pruning，依靠 benchmark 评估漏真率；
- **Strict-SP Try1**：不得开启任何可能删合法 SP 候选的 heuristic，除非该规则被证明为严格必要条件。

只有 Strict-SP 模式才可以谈有限 SP 假设空间内的完备性。

### 11.6 Engine B

Engine B 拟合形式上类似

\[
Z(s)=es+d+\frac{k_0}{s}
+\sum_i\frac{r_i}{s-p_i},
\]

通过极点重定位与留数重拟合估计有效阶数和极点结构，在映射物理可行时生成 Foster-like 候选，并为 Engine A 提供初值。

Engine B 很有价值，但除非 PR/passivity 被严格验证，否则其理论角色应保持为辅助模型。不能因为某次有限带、有噪声的 rational fit 没有出现预期极点，就推断某个 SP/RLC 拓扑“物理上不存在”。

### 11.7 当前 selector 的 systematics regime

当前 Try1 selector 先估计数据自身的局部噪声底，再根据

\[
\rho=\frac{\mathrm{wRMSE}_{best}}{\hat\sigma_{data}}
\]

判断是否进入 systematics-dominated regime。如果最佳候选残差远高于噪声底，说明所有候选都存在模型失配/夹具误差，此时继续让更大模型依靠拟合系统误差赢得 AICc 可能导致过拟合，因此当前逻辑改为在经验 adequacy band 内优先最少参数模型。

这是非常合理的工程策略，并已由项目真实数据 benchmark 支持；但它不是普适最大似然定理。`1.7`、`2.0`、`rho=1.4`、3σ clustering 等常数应继续作为**有版本、有 benchmark 来源的经验超参数**维护。

---

## 12. 数值可靠性要求

### 12.1 节点线性求解状态

当前 Try2/Try3 自研 complex LU 主要检测 pivot 是否为 0/非有限，对近奇异问题的诊断仍不足。论文级求解器应返回：

```text
OK | SINGULAR | ILL_CONDITIONED | NONFINITE | PORT_OPEN
```

并附带缩放 backward residual：

\[
\rho_{lin}
=
\frac{\|Yv-b\|}
{\|Y\|\|v\|+\|b\|},
\]

以及 reciprocal condition estimate。高 Q 或谐振附近，“pivot 非零”并不等于数值结果可信。

### 12.2 不应把奇异性隐藏成超大有限数

如果某个求解器在奇异时返回 `kBigZ` 这样的有限 sentinel，fit 层必须同时收到明确的 singular/nonfinite 标志，否则“数值失败”会伪装成“物理上阻抗很大”，影响目标函数和候选排序。

### 12.3 全项目统一连续优化器

推荐最终共用：

- 能用解析 Jacobian 就不用有限差分；
- 使用测量协方差白化残差；
- box/trust-region 或 LM；
- SVD/QR 求解增广最小二乘，而不是显式 normal equations；
- 返回完整 optimizer status；
- robust M-estimation/IRLS 作为可选、单独报告的外层。

### 12.4 可复现性

严格模式在同一输入/配置下必须 deterministic。若 multi-start 中使用随机初值，应输出 seed、启动数量和最优 start 信息，使 benchmark 失败可以精确复现。

---

## 13. 频率选择与实验设计

简单条件

\[
2M\ge p
\]

只说明实观测数量不小于实参数数量，是必要但远不充分的条件。真正重要的是 sensitivity information。

对固定拓扑和白化 Jacobian，

\[
\mathcal I(\theta)=J^TJ
\]

可视作局部 Fisher-information-like 矩阵。好的频率集合应避免灵敏度列近共线，并尽量覆盖：

- 可达到的低频/高频渐近区；
- 主要 RC/RL 拐点；
- 谐振和反谐振附近；
- 不同候选拓扑响应差异最大的区域。

可考虑主动实验设计：最大化 \(\sigma_{min}(J)\)、最小化条件数、D-optimal \(\det(J^TJ)\)，或在 topology discrimination 阶段选择“候选间阻抗分离度 / 测量噪声”最大的下一个频率。

对高 Q 网络尤其要注意：固定稀疏对数网格可能完全从一个窄谐振峰两侧跨过，导致关键动态不可见。最终仪器更适合在检测到疑似谐振后进行局部自适应加密。

---

## 14. 验证与验收协议

数学定理不能替代浮点实现测试；benchmark 也不能替代数学证明。最终项目必须同时维护两条证据链。

### 14.1 前向模型 property tests

至少应覆盖：单 R、C、L+DCR；串/并联 RC、RL、LC；串联 RLC；并联谐振器；有已知对称解的桥式网络；以及定义良好的低高频极限。

Metamorphic tests 应包括：

- 内部节点重编号不变性；
- 互易网络端口交换不变性；
- 任意边方向选择不变性；
- 完全相同器件置换不变性；
- 严格串/并联化简前后 \(Z\) 恒等；
- graph → output adjacency → graph round-trip 后阻抗一致。

### 14.2 独立求解器交叉验证

生产 nodal solver 应与独立编写的高精度或成熟矩阵库实现，在大量随机小图上比较。Try2 当前已有 long-double 独立 stamping cross-check，这是很好的基础，未来应提升为公共 core 测试。

### 14.3 枚举证明测试

对小 E 编写一个故意很慢、但逻辑极简单的 reference enumerator。最终比较：

\[
S_{prod}=S_{oracle},
\]

其中 S 是**完整 canonical signature set**，而不是只比较候选数量。同时保证 production set 无重复。

### 14.4 Jacobian 测试

解析 Jacobian 应与 central finite difference 在多组步长下比较；可行时再与 complex-step 或自动微分交叉验证。测试不仅覆盖普通参数，也必须覆盖尺度跨度大、高 Q、近奇异情况。

### 14.5 噪声与系统误差 Monte Carlo

已知真值的 synthetic campaign 应覆盖：

- 多档 Gaussian 噪声；
- heteroscedastic noise；
- 稀疏野点；
- 平滑幅度/相位系统误差；
- 器件容差；
- 频率网格稀疏；
- 高 Q；
- 近简并与严格等价网络。

报告至少包括：rank-1/top-K 等价类恢复率、fit calibration、参数 bias/variance、false-certainty rate、runtime，以及 Strict 与 Fast 模式分歧率。

当前已有 `real4`、`suite`、`suite2`、`realfam` 等验收门，它们应继续保留并版本化，但需要与上述 theorem-oriented property tests 同时存在。

---

## 15. 项目最终可以声称与不能声称的可靠性结论

### 15.1 前向求解器

**可以声称：** 对给定合法 R/C/L+DCR 图、有限参数且节点矩阵正则/条件良好时，统一 nodal formulation 在浮点误差范围内计算唯一线性端口阻抗；解析灵敏度公式由矩阵微分严格得到。

### 15.2 Strict Try2

**在条件成立时可以声称：** 若真网络属于声明有限候选空间，枚举和 canonicalization 完备，所有删枝均为安全规则，所有候选均完整评价或被严格 lower bound 排除，则 Strict Try2 返回该候选空间和目标函数上的全局最小值。

**不能声称：** 输出代表元一定是自然界唯一内部物理接线。

### 15.3 Fast Try2

**可以声称：** 在明确 benchmark 分布、噪声模型和配置下具有测得的高恢复率与速度优势。

**不能声称：** 在启发式 probe funnel 可删除候选的前提下仍然是无条件 exhaustive/global optimum。

### 15.4 Try3

**可以声称：** 若拟合点位于参数域内部，weighted Jacobian 满列秩且奇异值分离足够，多个独立初值收敛一致，残差与测量噪声模型相容，则参数的局部数值可辨识性得到强支持。

**不能声称：** 一次 LM 收敛或一次满秩数值检查即可证明全局唯一参数。

### 15.5 Try1

**可以声称：** 在当前 SP+synthesis 假设过程中找到了经过经验验证的最佳/简约候选或等价类。

**不能声称：** 对所有任意无源 RLC 内部拓扑做了完备恢复。

---

## 16. 最终规范软件架构

三套 Try 应逐步收敛到同一个数学核心：

```text
lcr_core/
  measurement_model      # covariance / weights / calibration metadata
  network_graph          # terminal colored multigraph + Edge
  components             # R, C, L+DCR laws
  nodal                  # Z, analytic Jacobian, solve diagnostics
  residual               # GLS / relative fallback / robust layer
  metrics                # RSS, wRMSE, maxRel, likelihood, AIC/AICc validity
  equivalence            # graph + observed/exact electrical equivalence
  numerics               # SVD/QR LM, linear solve diagnostics
```

在此基础上：

```text
Try2-Exact      = canonical graph enumeration + exact core evaluation
Try2-Tolerance  = Try2 enumeration + bounded common optimizer
Try3            = known graph/types + exact reduction + common optimizer
Try2.5          = Try2 topology enumeration + Try3 inner fit
Try1            = order/type/topology hypothesis generation + common optimizer
```

这样可以消除目前多份 weights、AICc、LU、LM、finite-difference refinement 和诊断代码之间的漂移风险。

### 16.1 推荐研发顺序

最终最清晰的研发顺序应为

\[
\boxed{
\text{Common Core}
\rightarrow
\text{Try2-Exact}
\rightarrow
\text{Try3}
\rightarrow
\text{Try2.5}
\rightarrow
\text{Try1}}
\]

每一步只引入一种新的不确定性，并要求上一层先具备独立可靠性。

---

## 17. 当前代码审计得到的主要修正项

在发表论文级结论前，最重要的实现/理论偏差如下：

1. **Try2 的结构生成是穷举，但当前默认 identify 流程不是严格穷举。** probe funnel 属于 heuristic。应增加 `STRICT` / `FAST` 模式并在输出中报告。
2. **Try1 是 SP 假设空间模型发现，不是任意图恢复。** 不得用一般 PR 综合存在性证明 SP 完备。
3. **Try1 F2 仍是破坏性剪枝。** Strict-SP 模式应禁用，除非未来给出严格必要条件证明。
4. **Try3 缺少完整 R0 articulation dead-zone reduction。** 应复用 Try2 R0。
5. **Try1/Try3 数值上不能精确表示 DCR=0，而理论和 Try2 可以。** 应增加非负边界参数支持。
6. **Try1 `exactN` 一般不是物理 BOM 数量。** 因为 canonical reduction 会消除端口不可分辨的冗余元件，应重命名/重写语义。
7. **Try2 tolerance refinement 维护了一套更弱的优化器。** 应改成公共解析 Jacobian + SVD-LM。
8. **Try1 Engine A 的 normal equations 应迁移到 SVD/QR 增广 LM。**
9. **当前自研 LU 缺少充分的近奇异诊断。** 应增加 backward residual 与 rcond estimate。
10. **当前 AICc 分母 clamp 改变了统计准则本身。** 当 \(n\le k+1\) 时应直接标记无效。
11. **当前 \(1/|Z|\) 权重在近零阻抗处需要 floor；最好直接使用测量协方差。**
12. **当前 sine-fit uncertainty 是近似传播。** 论文级链路应使用完整 LS covariance + delta method。
13. **当前 OSL calibration 是 scaffolding。** 模拟前端定型并建立对应误差模型之前，不能宣称已完成严格校准。
14. **有限扩展频率网格上的数值聚类不是符号电学等价证明。** 应命名为 `observed-band equivalence`。
15. **Jacobian rank/condition 是局部数值证据。** 单点 numerical rank 不能直接变成全局 structural identifiability 定理。

---

## 18. 推荐输出结构与科学 verdict

每个最终结果都应携带足够信息说明“为什么可信”：

```text
fit:
  objective, wrmse, max_relative_error
  noise_model / covariance_source
  robust_fit_used, outlier_count

search:
  hypothesis_family
  mode = STRICT | FAST
  structures_generated
  structures_canonical
  candidates_evaluated
  heuristic_pruning_used
  incumbent_bound_pruning_count

numerics:
  solve_status
  worst_linear_backward_error
  worst_rcond
  optimizer_status, starts, seed

identifiability:
  equivalence_class_id
  best_vs_second_gap
  jac_rank, jac_singular_values, jac_condition
  weak_parameters
  at_bound_parameters
  approximate_covariance/confidence_intervals

verdict:
  IDENTIFIABLE_LOCAL
  AMBIGUOUS_EQUIVALENCE_CLASS
  HYPOTHESIS_LIMITED
  NUMERICALLY_UNSTABLE
  DATA_INSUFFICIENT
  MODEL_MISMATCH_SUSPECTED
```

只返回一组 R/L/C 数值，而没有这些诊断，不能视为完整的科学辨识结果。

---

## 19. 参考文献

参考文献编号与英文版完全一致；为了保证 DOI、题名和期刊信息不因翻译产生歧义，正式引用保留英文原始文献信息。

[]{#ref-zh-1}**[1]** IEEE Std 1057-2017, *IEEE Standard for Digitizing Waveform Recorders*, Annex A: three-parameter known-frequency sine fitting.

[]{#ref-zh-2}**[2]** C.-W. Ho, A. E. Ruehli, and P. A. Brennan, “The modified nodal approach to network analysis,” *IEEE Transactions on Circuits and Systems*, 22(6), 504–509, 1975. DOI: https://doi.org/10.1109/TCS.1975.1084079

[]{#ref-zh-3}**[3]** O. Brune, “Synthesis of a Finite Two-terminal Network whose Driving-point Impedance is a Prescribed Function of Frequency,” *Journal of Mathematics and Physics*, 10, 191–236, 1931. DOI: https://doi.org/10.1002/sapm1931101191

[]{#ref-zh-4}**[4]** R. J. Duffin and R. Bott, “Impedance synthesis without use of transformers,” *Journal of Applied Physics*, 20(8), 816, 1949. DOI: https://doi.org/10.1063/1.1698532

[]{#ref-zh-5}**[5]** R. M. Foster, “A Reactance Theorem,” *Bell System Technical Journal*, 3, 259–267, 1924.

[]{#ref-zh-6}**[6]** W. Cauer, classical works on realization of prescribed frequency-dependent impedances and ladder synthesis, 1926–1927; see also Brune [3].

[]{#ref-zh-7}**[7]** S. Chaiken, “A Combinatorial Proof of the All Minors Matrix Tree Theorem,” *SIAM Journal on Algebraic Discrete Methods*, 3(3), 319–329, 1982. DOI: https://doi.org/10.1137/0603033

[]{#ref-zh-8}**[8]** H. Whitney, “2-Isomorphic Graphs,” *American Journal of Mathematics*, 55(1), 245–254, 1933. DOI: https://doi.org/10.2307/2371127

[]{#ref-zh-9}**[9]** B. Gustavsen and A. Semlyen, “Rational approximation of frequency domain responses by vector fitting,” *IEEE Transactions on Power Delivery*, 14(3), 1052–1061, 1999. DOI: https://doi.org/10.1109/61.772353

[]{#ref-zh-10}**[10]** D. W. Marquardt, “An Algorithm for Least-Squares Estimation of Nonlinear Parameters,” *Journal of the Society for Industrial and Applied Mathematics*, 11(2), 431–441, 1963. DOI: https://doi.org/10.1137/0111030

[]{#ref-zh-11}**[11]** T. J. Rothenberg, “Identification in Parametric Models,” *Econometrica*, 39(3), 577–591, 1971. DOI: https://doi.org/10.2307/1913267

[]{#ref-zh-12}**[12]** L. Ljung and T. Glad, “On global identifiability for arbitrary model parametrizations,” *Automatica*, 30(2), 265–276, 1994. DOI: https://doi.org/10.1016/0005-1098(94)90029-9

[]{#ref-zh-13}**[13]** H. Akaike, “A new look at the statistical model identification,” *IEEE Transactions on Automatic Control*, 19(6), 716–723, 1974. DOI: https://doi.org/10.1109/TAC.1974.1100705

[]{#ref-zh-14}**[14]** C. M. Hurvich and C.-L. Tsai, “Regression and time series model selection in small samples,” *Biometrika*, 76(2), 297–307, 1989. DOI: https://doi.org/10.1093/biomet/76.2.297

[]{#ref-zh-15}**[15]** P. J. Huber, “Robust Estimation of a Location Parameter,” *The Annals of Mathematical Statistics*, 35(1), 73–101, 1964. DOI: https://doi.org/10.1214/aoms/1177703732

[]{#ref-zh-16}**[16]** B. D. McKay and A. Piperno, “Practical graph isomorphism, II,” *Journal of Symbolic Computation*, 60, 94–112, 2014. DOI: https://doi.org/10.1016/j.jsc.2013.09.003

[]{#ref-zh-17}**[17]** Keysight Technologies, “1-Port Calibration (reflection test)” and “Measurement Errors”: one-port OSL calibration corrects directivity, source match, and reflection tracking errors.

---

## 附录 A——安全 partial-cost pruning 证明

设每个频率损失项满足

\[
\ell_k(G)\ge0.
\]

对任意频率排列 \(\pi\) 和 \(q<M\)：

\[
J(G)
=J_q(G)+\sum_{k=q+1}^M\ell_{\pi(k)}(G)
\ge J_q(G).
\]

若已经有一个完整评价的 incumbent 满足

\[
J_{best}<J_q(G),
\]

则必有

\[
J(G)>J_{best}.
\]

因此提前终止该候选不会删除全局最优解。若 robust/GLS 权重是预先固定且非负，该证明保持成立；如果权重在候选评价过程中根据候选本身动态改变，则必须针对新的目标函数重新建立 lower bound。

## 附录 B——Jacobian 局部可辨识条件证明

如果

\[
\operatorname{rank}J_h(\theta_\star)=p,
\]

则必存在一个 \(p\times p\) Jacobian minor 非奇异。选择对应的 p 个输出坐标组成

\[
g:\mathbb R^p\to\mathbb R^p.
\]

由逆函数定理，\(g\) 在 \(\theta_\star\) 邻域内存在唯一局部逆。因此在该邻域内，如果完整测量向量相同，则参数必须相同。该证明只保证**局部**可辨识，不保证整个参数域上的全局唯一性。

## 附录 C——为什么物理元件数量可能不可观测

如果两种物理连接对所有复频率都产生相同的 primitive equivalent，则任何单端口频率测量都无法判断内部究竟有几个封装。例如

\[
R_1+R_2=R_{eq},
\]

\[
C_1\parallel C_2=C_{eq},
\]

以及项目电感模型下

\[
R+(R_d+sL)=(R+R_d)+sL.
\]

因此 Try1 的 canonicalization 实际辨识的是**电学不可约实现**。如果 `exactN` 仍被定义成物理 BOM 数量，就会要求算法从端口数据中恢复本来不可观测的信息，这在理论上是不适定的。

---

# Final normative statement / 最终规范性结论

The project should not be organized as three unrelated fitters. It is one inverse-network framework with a common measurement model, a common terminal multigraph, a common nodal forward map and analytic Jacobian, and three levels of prior information. The scientifically strongest development path is to make the common core and Strict Try2 provable first, then make Try3 locally identifiable and numerically stable, then compose them as Try2.5, and only then expand Try1's hypothesis generator. Fast heuristics remain valuable, but every result must state whether it was obtained in a theorem-preserving strict mode or a benchmark-validated fast mode.

本项目最终不应呈现为三个互不相关的“拟合器”，而应呈现为一个统一的逆网络辨识框架：共同的测量模型、共同的二端多重图、共同的节点前向方程与解析 Jacobian，仅先验信息层级不同。最可靠的研发路线是先把公共核心与 Strict Try2 做成可证明模块，再把 Try3 做成具有明确局部可辨识性和数值稳定诊断的连续参数模块，之后组合成 Try2.5，最后才扩展 Try1 的模型假设生成能力。Fast heuristic 可以长期保留并用于工程性能，但每个结果都必须明确说明它来自“保持理论保证的 strict mode”，还是“经过 benchmark 验证的 fast mode”。

---

# Appendix D — Normative Algorithms and Source-to-Theory Audit Map

This appendix is normative. It converts the mathematical statements above into implementation-level algorithms while keeping strict and heuristic paths separate.

## Algorithm D.1 — Common forward model and Jacobian

**Input:** terminal multigraph \(G\), edge types/parameters \(\theta\), frequencies \(f_1,\ldots,f_M\).  
**Output:** \(Z_k\), complex parameter Jacobian, and numerical diagnostics.

```text
for each frequency f_k:
    s <- j 2π f_k
    construct reduced Y = 0
    for each edge e=(u,v):
        (y_e, dy_e/dtheta_e[*]) <- primitive_admittance_and_derivatives(e,s)
        stamp y_e [ +1 -1; -1 +1 ] into reduced Y

    b <- unit-current injection at terminal 1, terminal 0 grounded
    solve Y v = b with condition/backward-error diagnostics
    if solve is not reliable:
        return explicit numerical status for this frequency

    Z_k <- b^T v
    for each parameter q belonging to edge e:
        Δv_e <- v_u - v_v, with grounded node voltage 0
        dZ_k/dq <- -(dy_e/dq) (Δv_e)^2
```

The implementation shall never obtain the production graph Jacobian by finite differences when this analytic expression is available. Finite differences are a *verification oracle*, not the primary derivative.

## Algorithm D.2 — Strict Try2-Exact

```text
INPUT: exact ComponentSet C, measurements D, strict objective J

structures <- enumerate every active connected loop-free E-edge terminal multigraph
              for V = 2..E+1
structures <- canonicalize under internal relabeling and reciprocal terminal swap

best_cost <- +infinity
results <- empty

for each canonical structure S:
    for each component-assignment orbit A under Aut(S) and identical components:
        G <- (S,A)

        # Optional but theorem-preserving acceleration:
        partial_cost <- 0
        for f in a fixed discrimination-ordered frequency list:
            Z <- common_forward(G,f)
            partial_cost += nonnegative_loss(Z, measurement[f])
            if partial_cost > best_cost:
                reject G by safe lower bound
                break

        if not rejected:
            full_cost <- partial_cost  # equals complete sum after all frequencies
            update best_cost and tied candidates

cluster tied/near-tied outputs using a clearly labeled equivalence definition
return global minimizer(s) over the declared candidate set + completeness diagnostics
```

No probe-ratio candidate deletion is permitted in `STRICT` mode.

## Algorithm D.3 — Try3 parameter recovery

```text
INPUT: known graph G, known edge types, measurements D

G_reduced, parameter_groups <- exact_reduce(G)
    - remove self loops and disconnected pieces
    - apply full R0 articulation dead-zone removal
    - iteratively apply exact series/parallel identities

parameterize positive values in log coordinates
represent zero-DCR boundaries explicitly
build covariance-whitened residual r(theta)

starts <- deterministic physical starts
starts += resonance/asymptotic starts when justified

for each start:
    repeat until convergence/failure:
        (Z,J_complex,solve_diag) <- common_forward_and_jacobian(...)
        J_real <- whiten [Re J_complex; Im J_complex]
        solve augmented LM step by SVD/QR
        apply bounds/trust policy
    retain converged result and diagnostics

optionally perform separately reported robust reweight/refit
choose best statistically comparable converged solution

at final theta:
    SVD(J_real) -> rank, singular values, condition
    compute elasticity, at-bound flags, approximate covariance
    evaluate independent starts for basin consistency

return parameters + groups + fit + numerical + identifiability verdict
```

## Algorithm D.4 — Try2.5

```text
INPUT: component types/counts, broad parameter bounds, measurements D

for each canonical topology G generated by the Strict/Fast Try2 topology layer:
    fit theta_G using the common Try3 optimizer
    store objective, complexity, convergence, identifiability, numerical status

rank statistically comparable topology/parameter pairs
cluster observed-band or exact electrical equivalences
return classes, not merely one graph
```

In a strict finite bounded version, topology enumeration can be complete, but the inner nonlinear optimization is still not globally certified unless a global parameter method or validated bound is added. Therefore Try2.5 must report `TOPOLOGY_ENUMERATION_COMPLETE` separately from `CONTINUOUS_GLOBAL_OPTIMUM_CERTIFIED`.

## Algorithm D.5 — Try1 model discovery

```text
INPUT: measurements D, declared max canonical device count/depth/type domain

features <- robust/asymptotic descriptors (heuristic information only)
rational_models <- Engine-B-style rational fits
synthesis_starts <- physically admissible mappings from rational models

hypotheses <- canonical normalized SP grammar within declared bounds
if mode == FAST:
    hypotheses <- heuristic F2 scheduling/pruning, with pruning statistics reported
if mode == STRICT_SP:
    do not destructively remove a legal SP hypothesis by an unproved data heuristic

for each SP hypothesis H:
    fit its continuous parameters with the common SVD/QR optimizer
    use deterministic, feature, resonance, and synthesis starts
    retain optimizer/numerical diagnostics

merge direct SP fits and physically certified synthesis candidates
rank with a declared statistical/engineering model-selection policy
cluster behavioral equivalence classes

return:
    best class(es), hypothesis-family limits, fit, model-selection regime,
    numerical diagnostics, and identifiability evidence
```

The phrase “unknown topology” in Try1 output must always be accompanied by the searched topology family (currently bounded normalized SP plus synthesis candidates).

## D.6 Current source-to-theory map at the audit commit

| Theoretical responsibility | Current implementation | Audit status | Normative direction |
|---|---|---|---|
| Known-frequency sine LS | `backend/app/dsp/sine_fit.py::sine_fit` | Correct linear LS and phase convention | Add full covariance/clock diagnostics |
| Z from V/I fits | `backend/app/dsp/impedance.py::measure_impedance` | Correct amplitude/phase ratio; uncertainty approximate | Delta-method covariance of Re/Im Z |
| Calibration | `backend/app/dsp/calibration.py` | Explicit scaffolding | Replace with front-end-specific calibrated error model |
| Primitive R/C/L+DCR laws | Try1 `circuits.cpp`; Try2 `components.cpp`; Try3 `nodal.cpp` | Physically consistent for Rd>0; Try2 also supports Rd=0 | One common component law incl. Rd=0 boundary |
| SP forward model | Try1 `circuits.cpp::eval*` | Correct recursive SER/PAR equations | Keep as optimized adapter or map to graph core |
| SP analytic derivative | Try1 `circuits.cpp::evalJac` | Correct | Cross-check against graph Jacobian |
| SP canonical library | Try1 `library.cpp` | Complete only for declared normalized grammar/count/depth | State grammar limits explicitly |
| Try1 F2 | Try1 `pruning.cpp::pruneF2` | Finite-band heuristic used destructively | Disable in Strict-SP |
| Try1 F3 | `pruneTrees` / optimization log | Correctly demoted to scheduling | Keep non-destructive |
| Try1 continuous fit | `fit_engine_a.cpp` | Analytic J; normal-equation LM | Migrate to common augmented SVD/QR LM |
| Rational helper | `fit_engine_b.cpp` | VF/SK-style behavioral fit | Add explicit PR/passivity certification |
| Try1 selector | `selector.cpp` | Empirical noise/systematics regime | Keep thresholds benchmark-versioned |
| Try2 structure enumeration | `enumerate.cpp` | Strong small-E evidence of completeness | Add independent signature oracle |
| Try2 graph canonicalization | `graph.cpp` | Brute-force permutation canonicalization | Validate with colored graph oracle at larger E |
| Try2 R0 | `graph.cpp::hasDeadPart` | Correct broader articulation rule | Move to common graph reductions |
| Try2 forward solve | `nodal.cpp` | Correct stamping; limited near-singular diagnostics | Common solver with residual/rcond |
| Try2 probe funnel | `filters.cpp`; `identify.cpp` | Heuristic, unsafe for strict proof | FAST only |
| Try2 value refinement | `selector.cpp` | Useful; finite-difference J + normal equations | Reuse Try3 analytic J + SVD LM |
| Try3 exact reductions | `graph.cpp` | Most local reductions exact | Add full R0 articulation dead-zone removal |
| Try3 forward/Jacobian | `nodal.cpp::zAndJac` | Strongest common mathematical core | Promote to `lcr_core` |
| Try3 optimizer | `fit.cpp` | Augmented SVD-LM + multistart | Promote to common optimizer |
| Try3 rank/condition | `fit.cpp` SVD diagnostics | Correct as local numerical diagnostic | Add covariance/CI; avoid global overclaim |
| Metrics/AICc | duplicated across engines | Mostly consistent, invalid-domain clamp exists | One metrics module; AICc validity check |
| Equivalence clustering | Try1/Try2 selectors | Finite-grid noise-aware behavioral equivalence | Name exact vs observed-band equivalence separately |

## D.7 Publication-level acceptance checklist

Before a release is described as “theoretically validated,” all of the following should be machine-verifiable:

```text
[ ] common primitive laws agree bit/roundoff-wise across Trys
[ ] common nodal solver passes closed-form and independent-solver tests
[ ] analytic graph Jacobian passes derivative oracle tests
[ ] Strict Try2 canonical signature set equals reference oracle for supported small E
[ ] Strict Try2 uses no unproved destructive data funnel
[ ] Try3 removes complete R0 dead regions
[ ] zero-DCR semantics are explicit and test-covered
[ ] AICc is disabled outside its mathematical domain
[ ] near-zero |Z| weighting is bounded or covariance-whitened
[ ] singular/ill-conditioned solves cannot appear as ordinary physical samples
[ ] every Fast-mode heuristic is surfaced in result metadata
[ ] strict-vs-fast disagreement is measured on adversarial as well as random tests
[ ] parameter-recovery reports Jacobian singular spectrum and boundary state
[ ] Try1 output states the exact searched topology family and device-count semantics
[ ] measurement covariance and calibration version are attached to fit input metadata
```

---

# 附录 D（中文）——规范算法与“理论—源码”审计映射

本附录属于规范性内容：它把前文定理转换成可直接落地的算法，同时明确隔离 strict path 与 heuristic fast path。

## 算法 D.1——统一前向模型与 Jacobian

**输入：** 二端多重图 \(G\)、器件参数 \(\theta\)、频率 \(f_1,\ldots,f_M\)。  
**输出：** \(Z_k\)、复参数 Jacobian 与数值诊断。

```text
对每个频率 f_k:
    s <- j 2π f_k
    Y <- 0
    对每条边 e=(u,v):
        计算 y_e 及其对边参数的解析 dy_e/dtheta
        将 y_e [ +1 -1; -1 +1 ] stamp 到约化节点矩阵 Y

    b <- 节点1注入1A，节点0接地
    求解 Y v = b，同时计算条件数/后向误差诊断
    若求解不可靠:
        返回显式 numerical status，不能伪装成普通阻抗点

    Z_k <- b^T v
    对每个属于边 e 的参数 q:
        Δv_e <- v_u-v_v（接地节点电压为0）
        dZ_k/dq <- -(dy_e/dq)(Δv_e)^2
```

当解析图 Jacobian 已存在时，生产优化器不应再使用有限差分；有限差分只作为 derivative verification oracle。

## 算法 D.2——Strict Try2-Exact

```text
输入：精确 ComponentSet C、测量 D、严格目标 J

枚举 V=2..E+1 下所有活动连通无自环 E 边二端多重图
对内部节点重标号和互易端口交换做 canonicalization

best_cost <- +inf

对每个 canonical structure S:
    对 Aut(S) 和 identical components 商空间中的每个器件 assignment:
        G <- (S, assignment)

        partial_cost <- 0
        按固定“高区分度优先”频率顺序计算:
            Z <- common_forward(G,f)
            partial_cost += 非负损失
            若 partial_cost > best_cost:
                由严格 lower bound 提前淘汰 G
                break

        若未淘汰:
            更新完整 cost、最优候选及并列候选

按明确命名的等价定义聚类
返回候选空间全局最小值/并列类 + 完备性诊断
```

`STRICT` 模式禁止使用 probe-ratio 这种不能证明安全的候选删除规则。

## 算法 D.3——Try3 参数恢复

```text
输入：已知图 G、已知边类型、测量 D

G_reduced, groups <- exact_reduce(G)
    删除 self-loop / disconnected
    执行完整 R0 articulation dead-zone removal
    迭代执行严格串并联恒等归约

正参数使用 log 坐标
DCR=0 使用显式非负边界语义
构造 covariance-whitened residual r(theta)

生成确定性物理初值 + 必要时的谐振/渐近初值

对每个 start:
    迭代:
        用公共前向模型得到 Z、analytic J、solve diagnostics
        将复 J 拆成白化实 Jacobian
        用 SVD/QR 解 augmented LM step
        应用 bounds/trust policy
    保存收敛结果与完整诊断

可选：单独报告的 robust reweight/refit
从统计可比的收敛解中选择最优

最终：
    SVD(J) -> rank / singular values / condition
    计算 elasticity / at_bound / approximate covariance
    检查独立 starts 是否收敛到一致 basin

返回参数 + 可辨识 group + fit + numerical + identifiability verdict
```

## 算法 D.4——Try2.5

```text
输入：已知器件类型/数量、宽参数范围、测量 D

对 Try2 topology layer 产生的每个 canonical G:
    使用公共 Try3 optimizer 拟合 theta_G
    保存 objective / complexity / convergence / identifiability / numerical status

对统计上可比的 topology+parameter pair 排序
按 observed-band 或 exact electrical equivalence 聚类
返回等价类，而不是只返回一张图
```

即使 topology enumeration 完备，内部连续优化仍然是局部方法，除非未来加入可验证 global parameter solver。因此结果中必须分开报告：

```text
TOPOLOGY_ENUMERATION_COMPLETE
CONTINUOUS_GLOBAL_OPTIMUM_CERTIFIED
```

二者不能混为一谈。

## 算法 D.5——Try1 模型发现

```text
输入：测量 D、声明的最大 canonical device count / depth / type domain

features <- 鲁棒/渐近特征（只作为 heuristic information）
rational_models <- Engine-B 风格有理拟合
synthesis_starts <- 从物理可实现 rational model 产生综合候选/初值

hypotheses <- 声明边界内全部 canonical normalized SP grammar
若 mode == FAST:
    可以使用 F2 heuristic scheduling/pruning，但必须报告剪枝统计
若 mode == STRICT_SP:
    不得由未证明的数据启发式破坏性删除合法 SP hypothesis

对每个 SP hypothesis:
    使用公共 SVD/QR optimizer 拟合连续参数
    使用 deterministic / feature / resonance / synthesis starts
    保存 optimizer 与 numerical diagnostics

合并 SP 直接拟合与通过物理认证的 synthesis candidates
用明确声明的统计/工程 model-selection policy 排序
聚类行为等价类

返回：
    best class(es)
    hypothesis-family limit
    fit/model-selection regime
    numerical diagnostics
    identifiability evidence
```

Try1 结果中的“unknown topology”必须同时输出实际搜索过的 topology family；当前为 bounded normalized SP + synthesis candidates。

## D.6 当前源码—理论职责映射

| 理论职责 | 当前实现 | 审计结论 | 最终方向 |
|---|---|---|---|
| 已知频率 sine LS | `sine_fit.py::sine_fit` | LS 与相位约定正确 | 增加完整协方差/时钟诊断 |
| V/I → Z | `impedance.py::measure_impedance` | 幅相比正确；不确定度近似 | Re/Im Z 的 delta covariance |
| 校准 | `calibration.py` | 明确为 scaffolding | 替换为与模拟前端一致的误差模型 |
| R/C/L+DCR laws | 三引擎各自实现 | Rd>0 时一致；Try2 支持 Rd=0 | 合并公共 component law |
| SP 前向 | Try1 `circuits.cpp` | SER/PAR 正确 | 可保留高效 adapter，或映射 graph core |
| SP analytic J | Try1 `circuits.cpp::evalJac` | 正确 | 与 graph Jacobian 双向验证 |
| SP library | Try1 `library.cpp` | 仅对声明 grammar/count/depth 完备 | 输出中显式声明边界 |
| Try1 F2 | `pruning.cpp::pruneF2` | 有限带 destructive heuristic | Strict-SP 禁用 |
| Try1 F3 | `pruneTrees` + optimization log | 已正确降级为 scheduling | 保持非破坏性 |
| Try1 continuous fit | `fit_engine_a.cpp` | analytic J；normal-equation LM | 迁移公共 augmented SVD/QR LM |
| rational helper | `fit_engine_b.cpp` | VF/SK-style 行为拟合 | 加 PR/passivity certification |
| Try1 selector | `selector.cpp` | 经验 noise/systematics 分区 | 阈值必须 benchmark-versioned |
| Try2 枚举 | `enumerate.cpp` | 小 E 完备性证据较强 | 加独立 signature oracle |
| Try2 canonical | `graph.cpp` | 暴力 permutation canonical | 大 E 用成熟 colored-graph oracle 验证 |
| Try2 R0 | `graph.cpp::hasDeadPart` | 完整 articulation 规则正确 | 移入公共 graph reduction |
| Try2 nodal | `nodal.cpp` | stamping 正确；近奇异诊断不足 | 公共 solver + residual/rcond |
| Try2 probe funnel | `filters.cpp` / `identify.cpp` | heuristic，不支持 strict proof | 仅 FAST |
| Try2 refinement | `selector.cpp` | 有用；finite diff + normal eq | 复用 Try3 analytic J + SVD LM |
| Try3 reductions | `graph.cpp` | 局部规则大多严格 | 增加完整 R0 |
| Try3 Z/J | `nodal.cpp::zAndJac` | 当前最强公共数学核心 | 提升到 `lcr_core` |
| Try3 optimizer | `fit.cpp` | augmented SVD-LM + multistart | 提升为公共 optimizer |
| Try3 rank/cond | `fit.cpp` | 正确的局部数值诊断 | 增 covariance/CI，避免全局过度声称 |
| metrics/AICc | 三引擎重复 | 基本一致但存在 invalid-domain clamp | 统一 metrics + validity |
| equivalence | Try1/Try2 selector | finite-grid noise-aware 行为等价 | exact 与 observed-band 分名 |

## D.7 论文级发布前机器验收清单

```text
[ ] 三个 Try 的 primitive law 在公共输入上数值一致
[ ] 公共 nodal solver 通过闭式解与独立求解器测试
[ ] analytic graph Jacobian 通过 derivative oracle
[ ] Strict Try2 在支持的小 E 上 canonical signature set == reference oracle
[ ] Strict Try2 不使用未证明 destructive data funnel
[ ] Try3 能删除完整 R0 dead region
[ ] DCR=0 语义显式并有测试
[ ] AICc 在数学无效域自动禁用
[ ] 近零 |Z| 权重有 floor 或使用 covariance whitening
[ ] singular/ill-conditioned solve 不会伪装成普通物理阻抗
[ ] 所有 Fast heuristic 都写入 result metadata
[ ] adversarial 与 random 数据上都测 Strict-vs-Fast disagreement
[ ] 参数恢复输出完整 Jacobian singular spectrum 和 boundary state
[ ] Try1 输出明确 topology family 与 device-count 语义
[ ] fit 输入携带 measurement covariance 与 calibration version
```
