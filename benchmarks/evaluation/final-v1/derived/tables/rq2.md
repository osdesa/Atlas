# rq2: Fixed round-robin quantum effects

## gpu-short-s1024-q2

`rr-s1024-q2` relative to `rr-s1024-q1` in `gpu-short-independent`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 24.9918% | [-0.501371, 52.8058] | false | true | inconclusive |
| milestone-16-intel-xe | `completion_time_us` | 24.9817% | [0.2094, 52.8807] | true | true | environment_specific |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 29.4468% | [0.558561, 61.937] | true | true | environment_specific |
| milestone-16-intel-xe | `gpu_jain_fairness` | -0.269724% | [-6.44426, 6.53569] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 2.10357% | [-2.56109, 7.64549] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 2.10324% | [-2.49017, 7.81294] | false | false | environment_specific |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 0.78771% | [-3.38851, 5.09319] | false | false | environment_specific |
| milestone-16-lavapipe | `gpu_jain_fairness` | -2.5327% | [-4.6941, -0.321152] | true | false | inconclusive |

## gpu-short-s1024-q4

`rr-s1024-q4` relative to `rr-s1024-q1` in `gpu-short-independent`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 28.4973% | [3.39215, 58.3376] | true | true | environment_specific |
| milestone-16-intel-xe | `completion_time_us` | 28.4874% | [2.43958, 58.546] | true | true | environment_specific |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 29.2721% | [-0.444385, 63.589] | false | true | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | -4.94137% | [-12.9026, 3.7407] | false | false | environment_specific |
| milestone-16-lavapipe | `response_p95_us` | 4.78716% | [-3.63226, 17.1056] | false | false | environment_specific |
| milestone-16-lavapipe | `completion_time_us` | 4.78676% | [-3.53784, 17.4972] | false | false | environment_specific |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 1.82224% | [-5.07804, 8.70098] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | -6.52887% | [-9.35463, -3.91901] | true | true | environment_specific |

## gpu-short-s256-q2

`rr-s256-q2` relative to `rr-s256-q1` in `gpu-short-independent`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 4.55087% | [-5.68256, 15.8778] | false | false | inconclusive |
| milestone-16-intel-xe | `completion_time_us` | 4.5506% | [-5.86526, 15.8065] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 16.4628% | [3.79132, 31.7455] | true | true | environment_specific |
| milestone-16-intel-xe | `gpu_jain_fairness` | 1.21229% | [-2.39777, 4.82974] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 0.474442% | [-5.21492, 6.60774] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 0.474226% | [-5.29731, 6.70417] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 4.82934% | [-1.66215, 12.7743] | false | false | environment_specific |
| milestone-16-lavapipe | `gpu_jain_fairness` | -0.198374% | [-0.685684, 0.295904] | false | false | inconclusive |

## gpu-short-s256-q4

`rr-s256-q4` relative to `rr-s256-q1` in `gpu-short-independent`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 10.9033% | [-4.31175, 29.3906] | false | true | inconclusive |
| milestone-16-intel-xe | `completion_time_us` | 10.9026% | [-4.49706, 29.9738] | false | true | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 15.8944% | [0.919969, 31.6991] | true | true | replicated |
| milestone-16-intel-xe | `gpu_jain_fairness` | -1.1349% | [-5.4277, 3.26665] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | -0.874762% | [-6.75061, 5.98322] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | -0.874851% | [-6.88727, 5.78506] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 7.03985% | [0.134372, 15.7139] | true | true | replicated |
| milestone-16-lavapipe | `gpu_jain_fairness` | -1.11377% | [-1.92534, -0.345571] | true | false | inconclusive |

## gpu-long-s16384-q2

`rr-s16384x4-q2` relative to `rr-s16384x4-q1` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 30.371% | [2.79132, 67.9834] | true | true | environment_specific |
| milestone-16-intel-xe | `completion_time_us` | 23.7224% | [1.9905, 52.894] | true | true | environment_specific |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 4.6528% | [-9.71152, 20.3613] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | -0.636857% | [-1.08108, -0.214743] | true | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 6.25285% | [-0.0571532, 14.187] | false | true | environment_specific |
| milestone-16-lavapipe | `completion_time_us` | 1.79027% | [-2.34637, 6.42535] | false | false | environment_specific |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 0.0896363% | [-3.9606, 4.07336] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | -0.380381% | [-0.55964, -0.235349] | true | false | inconclusive |

## gpu-long-s16384-q4

`rr-s16384x4-q4` relative to `rr-s16384x4-q1` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 33.9461% | [9.94541, 60.5882] | true | true | replicated |
| milestone-16-intel-xe | `completion_time_us` | 26.183% | [4.64033, 51.7732] | true | true | environment_specific |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 4.32247% | [-13.2111, 25.2802] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | -2.79964% | [-3.6149, -2.03986] | true | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 16.7954% | [8.61005, 26.2538] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 3.83387% | [-1.69259, 10.1107] | false | false | environment_specific |
| milestone-16-lavapipe | `throughput_tasks_per_second` | -0.605345% | [-5.99258, 5.13288] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | -1.69532% | [-2.19493, -1.3411] | true | false | inconclusive |

## gpu-long-s4096-q2

`rr-s4096x2-q2` relative to `rr-s4096x2-q1` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 12.2045% | [3.96553, 20.7511] | true | true | environment_specific |
| milestone-16-intel-xe | `completion_time_us` | 9.58327% | [1.69879, 17.6592] | true | true | environment_specific |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 1.99442% | [-6.66176, 11.4915] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | -0.164625% | [-0.43509, 0.0958835] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 6.74687% | [-0.98156, 14.7053] | false | true | environment_specific |
| milestone-16-lavapipe | `completion_time_us` | 1.9437% | [-2.71504, 6.74443] | false | false | environment_specific |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 0.536087% | [-4.01286, 5.34104] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | -0.00832514% | [-0.0540927, 0.0405548] | false | false | inconclusive |

## gpu-long-s4096-q4

`rr-s4096x2-q4` relative to `rr-s4096x2-q1` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 12.1061% | [1.72392, 23.3042] | true | true | environment_specific |
| milestone-16-intel-xe | `completion_time_us` | 9.51967% | [0.62544, 19.3493] | true | true | environment_specific |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 3.50596% | [-5.60838, 13.3598] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | -0.386508% | [-0.744056, -0.0476585] | true | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 6.78358% | [-2.95499, 19.2552] | false | true | environment_specific |
| milestone-16-lavapipe | `completion_time_us` | 1.04943% | [-5.74516, 8.88913] | false | false | environment_specific |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 3.97137% | [-2.2716, 10.4414] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | -0.110985% | [-0.185595, -0.0420505] | true | false | inconclusive |

## mixed-balanced-s4096-q2

`rr-s4096x2-q2` relative to `rr-s4096x2-q1` in `mixed-balanced-layered`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 6.15611% | [-3.11764, 16.8972] | false | true | inconclusive |
| milestone-16-intel-xe | `completion_time_us` | 2.42893% | [-2.55581, 8.40418] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 0.534513% | [-4.24189, 5.20579] | false | false | inconclusive |
| milestone-16-intel-xe | `cpu_jain_fairness` | -0.0741947% | [-0.293806, 0.117294] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | 0.271089% | [-1.88574, 2.51882] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | -1.52537% | [-3.54751, 0.584215] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | -1.14596% | [-2.53615, 0.349103] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 1.46261% | [0.0347441, 2.89857] | true | false | inconclusive |
| milestone-16-lavapipe | `cpu_jain_fairness` | 0.040909% | [-0.0252304, 0.104916] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | 0.0525296% | [-1.57332, 1.58099] | false | false | inconclusive |

## mixed-balanced-s4096-q4

`rr-s4096x2-q4` relative to `rr-s4096x2-q1` in `mixed-balanced-layered`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 4.02864% | [-4.08913, 12.9755] | false | false | inconclusive |
| milestone-16-intel-xe | `completion_time_us` | 3.32191% | [-1.51384, 9.14504] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 0.439199% | [-4.26856, 5.27636] | false | false | inconclusive |
| milestone-16-intel-xe | `cpu_jain_fairness` | 0.0411541% | [-0.13313, 0.20644] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | 2.98342% | [0.345158, 5.71218] | true | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | -1.43738% | [-4.13994, 1.84991] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | -0.726908% | [-2.55603, 1.37845] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 1.29742% | [-0.53832, 3.03229] | false | false | inconclusive |
| milestone-16-lavapipe | `cpu_jain_fairness` | 0.0316077% | [-0.0400617, 0.10691] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | 0.176046% | [-2.53892, 2.45248] | false | false | inconclusive |

## mixed-balanced-s1024-q2

`rr-s1024-q2` relative to `rr-s1024-q1` in `mixed-balanced-layered`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 8.39248% | [-11.0122, 33.3434] | false | true | inconclusive |
| milestone-16-intel-xe | `completion_time_us` | 7.83156% | [-6.94191, 24.8845] | false | true | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 23.8162% | [5.93462, 45.4987] | true | true | environment_specific |
| milestone-16-intel-xe | `cpu_jain_fairness` | 0.0562087% | [-0.283331, 0.43757] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | 0.142275% | [-0.426302, 0.746631] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 3.76935% | [-1.17549, 9.90339] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 4.05766% | [-0.260494, 9.65715] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | -1.53056% | [-6.02537, 2.93434] | false | false | environment_specific |
| milestone-16-lavapipe | `cpu_jain_fairness` | -0.000207344% | [-0.0931151, 0.0973028] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | 0.382698% | [-0.101951, 0.914791] | false | false | inconclusive |

## mixed-balanced-s1024-q4

`rr-s1024-q4` relative to `rr-s1024-q1` in `mixed-balanced-layered`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 18.0735% | [-2.91136, 40.1988] | false | true | environment_specific |
| milestone-16-intel-xe | `completion_time_us` | 12.0307% | [-3.97016, 28.626] | false | true | environment_specific |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 16.0996% | [-0.220653, 34.9277] | false | true | inconclusive |
| milestone-16-intel-xe | `cpu_jain_fairness` | -0.0137025% | [-0.29609, 0.263212] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | 0.634087% | [0.0679944, 1.27543] | true | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 8.89627% | [1.27633, 19.1765] | true | true | environment_specific |
| milestone-16-lavapipe | `completion_time_us` | 7.71248% | [2.43716, 14.0965] | true | true | environment_specific |
| milestone-16-lavapipe | `throughput_tasks_per_second` | -2.73456% | [-7.47358, 2.63151] | false | false | inconclusive |
| milestone-16-lavapipe | `cpu_jain_fairness` | -0.0491087% | [-0.207702, 0.0588675] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | 0.448516% | [-0.083629, 0.995221] | false | false | inconclusive |

## mixed-priority-s4096-q2

`rr-s4096x2-q2` relative to `rr-s4096x2-q1` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 0.411719% | [-8.9392, 10.4229] | false | false | inconclusive |
| milestone-16-intel-xe | `completion_time_us` | -2.57071% | [-8.12866, 2.81936] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 6.75535% | [-0.0215517, 15.0467] | false | true | inconclusive |
| milestone-16-intel-xe | `cpu_jain_fairness` | -0.291307% | [-1.19745, 0.53182] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | 1.32859% | [-1.00843, 4.10054] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 5.37023% | [-3.51127, 16.4828] | false | true | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 3.61146% | [-0.314901, 8.96497] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | -1.40557% | [-4.84188, 1.95564] | false | false | inconclusive |
| milestone-16-lavapipe | `cpu_jain_fairness` | 0.459593% | [-0.20173, 1.30639] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | -1.05754% | [-3.91123, 1.70255] | false | false | inconclusive |

## mixed-priority-s4096-q4

`rr-s4096x2-q4` relative to `rr-s4096x2-q1` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 6.17815% | [-6.29955, 20.6164] | false | true | inconclusive |
| milestone-16-intel-xe | `completion_time_us` | 1.0136% | [-5.54846, 8.78133] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 5.76665% | [-1.42443, 14.0075] | false | true | inconclusive |
| milestone-16-intel-xe | `cpu_jain_fairness` | -0.0153362% | [-1.3286, 1.18692] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | 0.72023% | [-3.14774, 4.62432] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 6.61055% | [-4.4635, 22.3744] | false | true | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 4.18119% | [-2.00635, 14.4973] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 0.148378% | [-4.11617, 3.75325] | false | false | inconclusive |
| milestone-16-lavapipe | `cpu_jain_fairness` | 0.458955% | [-0.278609, 1.45152] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | -0.637261% | [-4.03947, 3.15529] | false | false | inconclusive |

## mixed-priority-s1024-q2

`rr-s1024-q2` relative to `rr-s1024-q1` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 11.8352% | [-2.804, 30.1872] | false | true | inconclusive |
| milestone-16-intel-xe | `completion_time_us` | 3.53862% | [-6.28469, 14.9805] | false | false | inconclusive |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 8.43601% | [-1.09395, 18.7846] | false | true | inconclusive |
| milestone-16-intel-xe | `cpu_jain_fairness` | 0.180127% | [-0.128867, 0.563481] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | -0.292117% | [-3.4169, 2.44077] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 2.0944% | [-2.14545, 6.31538] | false | false | inconclusive |
| milestone-16-lavapipe | `completion_time_us` | 2.67143% | [-0.879363, 6.20178] | false | false | inconclusive |
| milestone-16-lavapipe | `throughput_tasks_per_second` | 0.665846% | [-3.56689, 5.92686] | false | false | inconclusive |
| milestone-16-lavapipe | `cpu_jain_fairness` | -0.200147% | [-0.47384, 0.0231679] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | 0.0250301% | [-1.10328, 0.779905] | false | false | inconclusive |

## mixed-priority-s1024-q4

`rr-s1024-q4` relative to `rr-s1024-q1` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 14.0226% | [-3.10822, 33.3113] | false | true | environment_specific |
| milestone-16-intel-xe | `completion_time_us` | 3.65671% | [-6.61018, 15.7268] | false | false | environment_specific |
| milestone-16-intel-xe | `throughput_tasks_per_second` | 12.3039% | [1.55536, 23.878] | true | true | environment_specific |
| milestone-16-intel-xe | `cpu_jain_fairness` | 0.179425% | [-0.109922, 0.520392] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | 0.974569% | [-1.67253, 4.33155] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 12.3544% | [4.20937, 26.3286] | true | true | environment_specific |
| milestone-16-lavapipe | `completion_time_us` | 9.98843% | [4.12779, 17.8507] | true | true | environment_specific |
| milestone-16-lavapipe | `throughput_tasks_per_second` | -3.53548% | [-8.5387, 2.04859] | false | false | environment_specific |
| milestone-16-lavapipe | `cpu_jain_fairness` | 0.0493738% | [-0.215568, 0.32132] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | 0.835984% | [-0.189896, 1.79098] | false | false | inconclusive |
