# rq4: Cooperative slicing cost and utilization

## gpu-short-s1024-slicing

`rr-s1024-q1` relative to `fifo-unsliced` in `gpu-short-independent`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 833.414% | [665.661, 1003.19] | true | true | replicated |
| milestone-16-intel-xe | `response_p95_us` | 836.128% | [665.646, 1009.03] | true | true | replicated |
| milestone-16-intel-xe | `gpu_host_busy_fraction` | -1.05223% | [-2.72635, 0.797535] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_timestamp_busy_fraction` | -55.0449% | [-60.9189, -48.4474] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 568.958% | [528.145, 611.714] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 569.947% | [529.645, 613.682] | true | true | replicated |
| milestone-16-lavapipe | `gpu_host_busy_fraction` | -1.19766% | [-1.65854, -0.747338] | true | false | inconclusive |
| milestone-16-lavapipe | `gpu_timestamp_busy_fraction` | -17.1659% | [-21.3012, -12.6295] | true | true | replicated |

## gpu-short-s256-slicing

`rr-s256-q1` relative to `fifo-unsliced` in `gpu-short-independent`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 4558.36% | [3716.09, 5436.53] | true | true | replicated |
| milestone-16-intel-xe | `response_p95_us` | 4573.65% | [3740.27, 5477.77] | true | true | replicated |
| milestone-16-intel-xe | `gpu_host_busy_fraction` | -0.362221% | [-2.01519, 1.46867] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_timestamp_busy_fraction` | -54.7675% | [-61.5932, -46.6914] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 2586.8% | [2363.14, 2826.61] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 2591.19% | [2367.39, 2831.14] | true | true | replicated |
| milestone-16-lavapipe | `gpu_host_busy_fraction` | -1.35448% | [-1.84422, -0.891635] | true | false | inconclusive |
| milestone-16-lavapipe | `gpu_timestamp_busy_fraction` | -19.2926% | [-22.5183, -15.6391] | true | true | replicated |

## gpu-long-s16384-slicing

`rr-s16384x4-q1` relative to `fifo-unsliced` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 246.547% | [209.538, 284.678] | true | true | replicated |
| milestone-16-intel-xe | `response_p95_us` | 208.379% | [171.577, 244.902] | true | true | environment_specific |
| milestone-16-intel-xe | `gpu_host_busy_fraction` | -4.42005% | [-5.29407, -3.54992] | true | false | inconclusive |
| milestone-16-intel-xe | `gpu_timestamp_busy_fraction` | -33.9842% | [-36.4401, -31.4833] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 60.3276% | [50.0105, 74.0146] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 4.0884% | [-4.02216, 14.6886] | false | false | environment_specific |
| milestone-16-lavapipe | `gpu_host_busy_fraction` | -1.78048% | [-1.98601, -1.64013] | true | false | inconclusive |
| milestone-16-lavapipe | `gpu_timestamp_busy_fraction` | -18.4675% | [-19.7171, -17.3184] | true | true | replicated |

## gpu-long-s4096-slicing

`rr-s4096x2-q1` relative to `fifo-unsliced` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 1522.25% | [1358.41, 1693.12] | true | true | replicated |
| milestone-16-intel-xe | `response_p95_us` | 1172.21% | [1040.93, 1308.27] | true | true | replicated |
| milestone-16-intel-xe | `gpu_host_busy_fraction` | -7.01624% | [-7.47989, -6.56808] | true | true | environment_specific |
| milestone-16-intel-xe | `gpu_timestamp_busy_fraction` | -70.3538% | [-72.2261, -68.4111] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 373.79% | [340.034, 413.889] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 211.83% | [181.868, 246.445] | true | true | replicated |
| milestone-16-lavapipe | `gpu_host_busy_fraction` | -4.32903% | [-4.54212, -4.12291] | true | false | environment_specific |
| milestone-16-lavapipe | `gpu_timestamp_busy_fraction` | -43.8498% | [-44.2162, -43.4298] | true | true | replicated |

## mixed-balanced-s4096-slicing

`rr-s4096x2-q1` relative to `fifo-unsliced` in `mixed-balanced-layered`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 8.78607% | [5.4157, 12.8102] | true | true | environment_specific |
| milestone-16-intel-xe | `response_p95_us` | 15.4524% | [7.74985, 24.7473] | true | true | environment_specific |
| milestone-16-intel-xe | `cpu_busy_fraction` | -5.98694% | [-8.50712, -3.70078] | true | true | environment_specific |
| milestone-16-intel-xe | `gpu_host_busy_fraction` | 141.645% | [116.69, 170.191] | true | true | replicated |
| milestone-16-intel-xe | `gpu_timestamp_busy_fraction` | 35.0107% | [18.0741, 53.2719] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 3.1415% | [-0.106547, 6.84652] | false | false | environment_specific |
| milestone-16-lavapipe | `response_p95_us` | 1.58405% | [-0.89009, 4.38132] | false | false | environment_specific |
| milestone-16-lavapipe | `cpu_busy_fraction` | -3.74638% | [-6.58963, -1.31591] | true | false | environment_specific |
| milestone-16-lavapipe | `gpu_host_busy_fraction` | 112.29% | [95.1993, 130.205] | true | true | replicated |
| milestone-16-lavapipe | `gpu_timestamp_busy_fraction` | 63.1273% | [50.0512, 77.5189] | true | true | replicated |

## mixed-balanced-s1024-slicing

`rr-s1024-q1` relative to `fifo-unsliced` in `mixed-balanced-layered`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 321.662% | [256.91, 386.748] | true | true | replicated |
| milestone-16-intel-xe | `response_p95_us` | 648.166% | [503.583, 794.317] | true | true | replicated |
| milestone-16-intel-xe | `cpu_busy_fraction` | -65.737% | [-69.0639, -62.3628] | true | true | replicated |
| milestone-16-intel-xe | `gpu_host_busy_fraction` | 366.322% | [330.44, 404.976] | true | true | replicated |
| milestone-16-intel-xe | `gpu_timestamp_busy_fraction` | 8.7475% | [-1.79552, 20.5965] | false | true | environment_specific |
| milestone-16-lavapipe | `completion_time_us` | 182.125% | [165.458, 202.535] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 308.689% | [270.37, 355.612] | true | true | replicated |
| milestone-16-lavapipe | `cpu_busy_fraction` | -62.7573% | [-64.5377, -61.0328] | true | true | replicated |
| milestone-16-lavapipe | `gpu_host_busy_fraction` | 230.438% | [207.1, 253.363] | true | true | replicated |
| milestone-16-lavapipe | `gpu_timestamp_busy_fraction` | 98.535% | [85.3405, 110.867] | true | true | environment_specific |

## mixed-priority-s4096-slicing

`rr-s4096x2-q1` relative to `fifo-unsliced` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 22.5016% | [13.9551, 34.0084] | true | true | replicated |
| milestone-16-intel-xe | `response_p95_us` | 15.7805% | [-2.56086, 37.5038] | false | true | environment_specific |
| milestone-16-intel-xe | `cpu_busy_fraction` | -13.5171% | [-18.0314, -9.19189] | true | true | replicated |
| milestone-16-intel-xe | `gpu_host_busy_fraction` | 129.867% | [103.348, 156.271] | true | true | replicated |
| milestone-16-intel-xe | `gpu_timestamp_busy_fraction` | 31.6627% | [12.3453, 52.2945] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 26.2674% | [18.5016, 34.7333] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 42.6083% | [17.4728, 68.0985] | true | true | environment_specific |
| milestone-16-lavapipe | `cpu_busy_fraction` | -20.9807% | [-25.0182, -16.9944] | true | true | replicated |
| milestone-16-lavapipe | `gpu_host_busy_fraction` | 75.233% | [60.9329, 89.7236] | true | true | replicated |
| milestone-16-lavapipe | `gpu_timestamp_busy_fraction` | 34.8999% | [23.7514, 47.4658] | true | true | replicated |

## mixed-priority-s1024-slicing

`rr-s1024-q1` relative to `fifo-unsliced` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `completion_time_us` | 414.978% | [364.862, 473.231] | true | true | replicated |
| milestone-16-intel-xe | `response_p95_us` | 607.584% | [438.119, 784.12] | true | true | replicated |
| milestone-16-intel-xe | `cpu_busy_fraction` | -75.8898% | [-77.7351, -73.7495] | true | true | replicated |
| milestone-16-intel-xe | `gpu_host_busy_fraction` | 202.419% | [171.71, 233.847] | true | true | replicated |
| milestone-16-intel-xe | `gpu_timestamp_busy_fraction` | -33.5121% | [-42.0113, -25.0231] | true | true | divergent |
| milestone-16-lavapipe | `completion_time_us` | 379.699% | [345.947, 415.354] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 601.977% | [436.588, 771.707] | true | true | replicated |
| milestone-16-lavapipe | `cpu_busy_fraction` | -78.2934% | [-79.6026, -76.8437] | true | true | replicated |
| milestone-16-lavapipe | `gpu_host_busy_fraction` | 93.92% | [77.5367, 111.603] | true | true | replicated |
| milestone-16-lavapipe | `gpu_timestamp_busy_fraction` | 18.2148% | [8.60864, 28.8295] | true | true | divergent |

## gpu-long-slice-cost

| Environment | Variant | Metric | Mean | 95% CI |
| --- | --- | --- | ---: | ---: |
| milestone-16-intel-xe | `rr-s16384x4-q1` | `immediate_slice_switch_mean_us` |  | [, ] |
| milestone-16-intel-xe | `rr-s16384x4-q1` | `control_active_fraction` | 0.00600627 | [0.00550988, 0.00657523] |
| milestone-16-intel-xe | `rr-s16384x4-q1` | `gpu_host_busy_fraction` | 0.931967 | [0.923606, 0.93988] |
| milestone-16-intel-xe | `rr-s16384x4-q1` | `gpu_timestamp_busy_fraction` | 0.578859 | [0.559105, 0.599845] |
| milestone-16-intel-xe | `rr-s4096x2-q1` | `immediate_slice_switch_mean_us` |  | [, ] |
| milestone-16-intel-xe | `rr-s4096x2-q1` | `control_active_fraction` | 0.00846448 | [0.00809859, 0.00884014] |
| milestone-16-intel-xe | `rr-s4096x2-q1` | `gpu_host_busy_fraction` | 0.906629 | [0.902581, 0.910185] |
| milestone-16-intel-xe | `rr-s4096x2-q1` | `gpu_timestamp_busy_fraction` | 0.260157 | [0.243946, 0.277867] |
| milestone-16-lavapipe | `rr-s16384x4-q1` | `immediate_slice_switch_mean_us` |  | [, ] |
| milestone-16-lavapipe | `rr-s16384x4-q1` | `control_active_fraction` | 0.00356086 | [0.00340942, 0.00372367] |
| milestone-16-lavapipe | `rr-s16384x4-q1` | `gpu_host_busy_fraction` | 0.978191 | [0.976242, 0.979541] |
| milestone-16-lavapipe | `rr-s16384x4-q1` | `gpu_timestamp_busy_fraction` | 0.77828 | [0.763956, 0.791171] |
| milestone-16-lavapipe | `rr-s4096x2-q1` | `immediate_slice_switch_mean_us` |  | [, ] |
| milestone-16-lavapipe | `rr-s4096x2-q1` | `control_active_fraction` | 0.00636669 | [0.00621635, 0.00650988] |
| milestone-16-lavapipe | `rr-s4096x2-q1` | `gpu_host_busy_fraction` | 0.95281 | [0.950533, 0.954947] |
| milestone-16-lavapipe | `rr-s4096x2-q1` | `gpu_timestamp_busy_fraction` | 0.535897 | [0.532805, 0.538951] |

## mixed-balanced-utilization

| Environment | Variant | Metric | Mean | 95% CI |
| --- | --- | --- | ---: | ---: |
| milestone-16-intel-xe | `fifo-unsliced` | `control_active_fraction` | 0.00338615 | [0.00308512, 0.0037204] |
| milestone-16-intel-xe | `fifo-unsliced` | `cpu_busy_fraction` | 0.774148 | [0.753968, 0.796853] |
| milestone-16-intel-xe | `fifo-unsliced` | `gpu_host_busy_fraction` | 0.214409 | [0.193552, 0.238994] |
| milestone-16-intel-xe | `fifo-unsliced` | `gpu_timestamp_busy_fraction` | 0.136414 | [0.117826, 0.157905] |
| milestone-16-intel-xe | `rr-s4096x2-q1` | `control_active_fraction` | 0.00538948 | [0.00509498, 0.00570604] |
| milestone-16-intel-xe | `rr-s4096x2-q1` | `cpu_busy_fraction` | 0.727264 | [0.703179, 0.751649] |
| milestone-16-intel-xe | `rr-s4096x2-q1` | `gpu_host_busy_fraction` | 0.46615 | [0.428625, 0.50583] |
| milestone-16-intel-xe | `rr-s4096x2-q1` | `gpu_timestamp_busy_fraction` | 0.156101 | [0.139581, 0.175417] |
| milestone-16-intel-xe | `rr-s1024-q1` | `control_active_fraction` | 0.00916263 | [0.00882194, 0.00950326] |
| milestone-16-intel-xe | `rr-s1024-q1` | `cpu_busy_fraction` | 0.264481 | [0.239373, 0.289031] |
| milestone-16-intel-xe | `rr-s1024-q1` | `gpu_host_busy_fraction` | 0.879071 | [0.859561, 0.89486] |
| milestone-16-intel-xe | `rr-s1024-q1` | `gpu_timestamp_busy_fraction` | 0.11941 | [0.113803, 0.124787] |
| milestone-16-lavapipe | `fifo-unsliced` | `control_active_fraction` | 0.00344653 | [0.00326907, 0.00363306] |
| milestone-16-lavapipe | `fifo-unsliced` | `cpu_busy_fraction` | 0.776629 | [0.753402, 0.802207] |
| milestone-16-lavapipe | `fifo-unsliced` | `gpu_host_busy_fraction` | 0.295404 | [0.272403, 0.321987] |
| milestone-16-lavapipe | `fifo-unsliced` | `gpu_timestamp_busy_fraction` | 0.246373 | [0.226917, 0.268698] |
| milestone-16-lavapipe | `rr-s4096x2-q1` | `control_active_fraction` | 0.00574313 | [0.00550135, 0.00602812] |
| milestone-16-lavapipe | `rr-s4096x2-q1` | `cpu_busy_fraction` | 0.745819 | [0.739797, 0.751684] |
| milestone-16-lavapipe | `rr-s4096x2-q1` | `gpu_host_busy_fraction` | 0.593614 | [0.567177, 0.620972] |
| milestone-16-lavapipe | `rr-s4096x2-q1` | `gpu_timestamp_busy_fraction` | 0.381397 | [0.361409, 0.402257] |
| milestone-16-lavapipe | `rr-s1024-q1` | `control_active_fraction` | 0.00840966 | [0.00823966, 0.0085773] |
| milestone-16-lavapipe | `rr-s1024-q1` | `cpu_busy_fraction` | 0.288325 | [0.279419, 0.296799] |
| milestone-16-lavapipe | `rr-s1024-q1` | `gpu_host_busy_fraction` | 0.92074 | [0.893627, 0.94019] |
| milestone-16-lavapipe | `rr-s1024-q1` | `gpu_timestamp_busy_fraction` | 0.463307 | [0.447436, 0.477279] |
