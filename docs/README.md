# Thoth documentation

Guides to running and understanding the engine. Start with the project
[`README.md`](../README.md) for the overview, then dive in here.

| Guide | What it covers |
|-------|----------------|
| [running.md](running.md) | Building, the four `thoth` CLI modes, and the `run_*` Docker wrappers (batch / server / cluster) with examples. |
| [products.md](products.md) | Supported instruments and underlyings, their YAML fields, and the per-engine support matrix. |
| [volatility.md](volatility.md) | Volatility models end to end: constant, SABR, implied→Dupire local vol, the Milstein step, Heston and Bates. |
| [monte_carlo.md](monte_carlo.md) | The MCL / AMC node-graph engine: pseudo vs Sobol QMC, the Brownian bridge, diffusion schemes, American LSM, cluster path-splitting. |
| [pde.md](pde.md) | The finite-difference engine: the sinh-transformed Crank-Nicolson 1-D solver, barriers/American, and the 2-D Heston Douglas-ADI extension. |
| [gpu.md](gpu.md) | The CUDA `mcl_gpu` backend: scope, the build gate, automatic CPU fallback, and the kernel. |
| [agent.md](agent.md) | Working on Thoth as the IT Quant Agent: the README-sync mandate, build/test/format workflow, and conventions. |
