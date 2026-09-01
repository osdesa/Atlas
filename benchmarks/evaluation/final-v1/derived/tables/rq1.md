# rq1: Scheduler coordination cost

## cpu-short-fifo

`fifo-unsliced` relative to `direct` in `cpu-short-independent`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 1.76068% | [-1.0679, 4.6447] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | -0.969814% | [-3.74804, 1.96488] | false | false | inconclusive |
| milestone-16-intel-xe | `response_p95_us` | 1.43388% | [-1.56756, 4.26003] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 0.418029% | [-2.19805, 2.69257] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 0.805859% | [-2.14444, 5.55963] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 0.24327% | [-2.49039, 2.66519] | false | false | inconclusive |

## cpu-long-fifo

`fifo-unsliced` relative to `direct` in `cpu-long-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 1.30274% | [-0.965099, 3.62442] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | -0.28591% | [-2.48594, 2.02858] | false | false | inconclusive |
| milestone-16-intel-xe | `response_p95_us` | 1.75471% | [-1.10532, 4.76208] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 2.08998% | [-1.56124, 5.9096] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | -0.571574% | [-4.10098, 3.05461] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 2.85281% | [-1.2868, 7.36805] | false | false | inconclusive |

## gpu-short-fifo

`fifo-unsliced` relative to `direct` in `gpu-short-independent`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 6.64977% | [-12.7678, 33.8586] | false | true | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 42.2236% | [15.0143, 73.615] | true | true | environment_specific |
| milestone-16-intel-xe | `response_p95_us` | 6.39913% | [-13.0208, 33.2736] | false | true | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 7.03675% | [-3.83801, 18.741] | false | true | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 5.94912% | [-4.68221, 17.9607] | false | true | environment_specific |
| milestone-16-lavapipe | `response_p95_us` | 6.89531% | [-4.04837, 18.0735] | false | true | inconclusive |

## gpu-long-fifo

`fifo-unsliced` relative to `direct` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | -3.43204% | [-11.0952, 4.56874] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 10.9311% | [3.08775, 19.6337] | true | true | environment_specific |
| milestone-16-intel-xe | `response_p95_us` | -0.93422% | [-9.12376, 8.38983] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 3.40164% | [-0.565219, 7.0602] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | -1.5262% | [-5.45111, 3.70041] | false | false | environment_specific |
| milestone-16-lavapipe | `response_p95_us` | 4.08514% | [-0.448221, 8.25012] | false | false | inconclusive |

## mixed-balanced-fifo

`fifo-unsliced` relative to `direct` in `mixed-balanced-layered`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 0.0567781% | [-1.46963, 1.43533] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 0.346114% | [-1.12314, 2.10334] | false | false | inconclusive |
| milestone-16-intel-xe | `response_p95_us` | 0.867332% | [-1.21471, 2.85921] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | -2.71531% | [-4.19629, -1.03557] | true | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 3.10149% | [1.45529, 4.64805] | true | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | -2.8286% | [-4.45669, -1.00302] | true | false | inconclusive |

## mixed-priority-fifo

`fifo-unsliced` relative to `direct` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | -0.0570461% | [-1.97644, 2.53094] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 0.608272% | [-1.33532, 2.34713] | false | false | inconclusive |
| milestone-16-intel-xe | `response_p95_us` | 2.8588% | [-0.675255, 7.3048] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | -3.90189% | [-5.61107, -2.04159] | true | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 4.59747% | [2.7342, 6.4466] | true | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | -4.10867% | [-7.11521, -1.18985] | true | false | inconclusive |

## scheduler-active

| Environment | Variant | Metric | Mean | 95% CI |
| --- | --- | --- | ---: | ---: |
| milestone-16-intel-xe | `fifo-unsliced` | `control_active_fraction` | 0.0287311 | [0.0262703, 0.0314346] |
| milestone-16-lavapipe | `fifo-unsliced` | `control_active_fraction` | 0.027242 | [0.0252592, 0.0293614] |
