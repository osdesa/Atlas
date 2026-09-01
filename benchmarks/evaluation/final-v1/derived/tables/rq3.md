# rq3: Static-priority response and fairness effects

## cpu-long-priority

`priority-unsliced` relative to `fifo-unsliced` in `cpu-long-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 20.4463% | [10.6458, 30.9474] | true | true | replicated |
| milestone-16-intel-xe | `completion_time_us` | 1.85677% | [-1.73332, 5.93179] | false | false | inconclusive |
| milestone-16-intel-xe | `ready_wait_p95_us` | 39.6315% | [21.7862, 59.5047] | true | true | replicated |
| milestone-16-intel-xe | `selection_bypass_max` | 65.2571% | [48.1, 85.15] | true | true | replicated |
| milestone-16-intel-xe | `cpu_jain_fairness` | 1.88232% | [1.22738, 2.58495] | true | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 14.6896% | [6.66907, 22.8142] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 1.02276% | [-2.16286, 4.21008] | false | false | inconclusive |
| milestone-16-lavapipe | `ready_wait_p95_us` | 33.4955% | [19.2271, 48.0945] | true | true | replicated |
| milestone-16-lavapipe | `selection_bypass_max` | 65.2738% | [51.4881, 82.6] | true | true | replicated |
| milestone-16-lavapipe | `cpu_jain_fairness` | 1.66329% | [1.17841, 2.22487] | true | false | inconclusive |

## gpu-long-priority

`priority-unsliced` relative to `fifo-unsliced` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 55.5057% | [36.3232, 74.2636] | true | true | replicated |
| milestone-16-intel-xe | `completion_time_us` | -0.291274% | [-7.03431, 7.16963] | false | false | inconclusive |
| milestone-16-intel-xe | `ready_wait_p95_us` | 67.2735% | [43.6749, 89.9131] | true | true | replicated |
| milestone-16-intel-xe | `selection_bypass_max` | 72.5% | [45, 100] | true | true | replicated |
| milestone-16-intel-xe | `gpu_jain_fairness` | -4.36649% | [-9.87362, 1.45377] | false | false | environment_specific |
| milestone-16-lavapipe | `response_p95_us` | 56.1363% | [30.5868, 81.8336] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 2.81171% | [-1.11868, 8.76967] | false | false | inconclusive |
| milestone-16-lavapipe | `ready_wait_p95_us` | 70.1659% | [39.1362, 101.577] | true | true | replicated |
| milestone-16-lavapipe | `selection_bypass_max` | 72.5% | [42.5, 100] | true | true | replicated |
| milestone-16-lavapipe | `gpu_jain_fairness` | -6.20237% | [-11.4475, -0.854584] | true | true | environment_specific |

## gpu-long-s16384-priority

`priority-s16384x4` relative to `rr-s16384x4-q1` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 205.689% | [124.753, 305.525] | true | true | replicated |
| milestone-16-intel-xe | `completion_time_us` | 39.2683% | [17.7546, 64.2622] | true | true | environment_specific |
| milestone-16-intel-xe | `ready_wait_p95_us` | 290.914% | [174.958, 437.836] | true | true | replicated |
| milestone-16-intel-xe | `selection_bypass_max` | 263.824% | [195.882, 327.941] | true | true | replicated |
| milestone-16-intel-xe | `gpu_jain_fairness` | -31.059% | [-36.0037, -26.0836] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 147.273% | [102.308, 191.663] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | -4.76284% | [-10.5758, 1.3752] | false | false | environment_specific |
| milestone-16-lavapipe | `ready_wait_p95_us` | 219.616% | [156.054, 284.588] | true | true | replicated |
| milestone-16-lavapipe | `selection_bypass_max` | 263.824% | [196.176, 328.235] | true | true | replicated |
| milestone-16-lavapipe | `gpu_jain_fairness` | -30.7197% | [-35.0347, -26.1576] | true | true | replicated |

## gpu-long-s4096-priority

`priority-s4096x2` relative to `rr-s4096x2-q1` in `gpu-long-contention`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 140.494% | [97.2656, 186.642] | true | true | replicated |
| milestone-16-intel-xe | `completion_time_us` | 6.74754% | [-3.61926, 17.5483] | false | true | inconclusive |
| milestone-16-intel-xe | `ready_wait_p95_us` | 208.009% | [146.625, 270.839] | true | true | replicated |
| milestone-16-intel-xe | `selection_bypass_max` | 286.55% | [212.442, 355.969] | true | true | replicated |
| milestone-16-intel-xe | `gpu_jain_fairness` | -31.3996% | [-35.8625, -26.8167] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 158.939% | [119.371, 198.294] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | -1.28518% | [-6.62893, 4.30092] | false | false | inconclusive |
| milestone-16-lavapipe | `ready_wait_p95_us` | 234.803% | [174.652, 292.385] | true | true | replicated |
| milestone-16-lavapipe | `selection_bypass_max` | 286.55% | [216.977, 355.853] | true | true | replicated |
| milestone-16-lavapipe | `gpu_jain_fairness` | -31.1673% | [-35.7145, -26.5376] | true | true | replicated |

## mixed-priority-unsliced

`priority-unsliced` relative to `fifo-unsliced` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 19.7155% | [7.05691, 36.195] | true | true | replicated |
| milestone-16-intel-xe | `completion_time_us` | 4.6004% | [-0.888988, 10.5353] | false | false | inconclusive |
| milestone-16-intel-xe | `ready_wait_p95_us` | 30.3488% | [12.5519, 53.7918] | true | true | replicated |
| milestone-16-intel-xe | `selection_bypass_max` | 41.4988% | [23.0163, 63] | true | true | replicated |
| milestone-16-intel-xe | `cpu_jain_fairness` | -0.298509% | [-1.6358, 1.3628] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | 0.872515% | [-2.17622, 3.87174] | false | false | inconclusive |
| milestone-16-lavapipe | `response_p95_us` | 15.2962% | [4.99609, 28.0939] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 1.53432% | [-2.26272, 5.52298] | false | false | inconclusive |
| milestone-16-lavapipe | `ready_wait_p95_us` | 26.214% | [10.8946, 44.6531] | true | true | replicated |
| milestone-16-lavapipe | `selection_bypass_max` | 47.127% | [26.4698, 70.6643] | true | true | replicated |
| milestone-16-lavapipe | `cpu_jain_fairness` | 0.0500404% | [-1.43638, 1.73252] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | 0.417663% | [-2.75355, 3.58655] | false | false | inconclusive |

## mixed-s4096-priority

`priority-s4096x2` relative to `rr-s4096x2-q1` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 24.6414% | [8.13116, 43.426] | true | true | replicated |
| milestone-16-intel-xe | `completion_time_us` | -0.0641354% | [-6.1972, 6.68132] | false | false | environment_specific |
| milestone-16-intel-xe | `ready_wait_p95_us` | 47.6885% | [22.2705, 75.3203] | true | true | replicated |
| milestone-16-intel-xe | `selection_bypass_max` | 85.4778% | [42.3672, 133.168] | true | true | replicated |
| milestone-16-intel-xe | `cpu_jain_fairness` | -0.57303% | [-1.73504, 0.569225] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | -6.21148% | [-13.2998, 0.580438] | false | true | environment_specific |
| milestone-16-lavapipe | `response_p95_us` | 59.7237% | [22.4659, 114.053] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 11.2562% | [5.39213, 17.704] | true | true | environment_specific |
| milestone-16-lavapipe | `ready_wait_p95_us` | 80.8613% | [31.2904, 148.497] | true | true | replicated |
| milestone-16-lavapipe | `selection_bypass_max` | 93.7479% | [47.6794, 141.753] | true | true | replicated |
| milestone-16-lavapipe | `cpu_jain_fairness` | -0.0435763% | [-1.02737, 0.869406] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | -9.28821% | [-16.7693, -1.64769] | true | true | environment_specific |

## mixed-s1024-priority

`priority-s1024` relative to `rr-s1024-q1` in `mixed-priority-bursty`.

| Environment | Metric | Mean change | 95% CI | Stable | Material | Cross-environment |
| --- | --- | ---: | ---: | --- | --- | --- |
| milestone-16-intel-xe | `response_p95_us` | 68.1998% | [18.708, 140.54] | true | true | replicated |
| milestone-16-intel-xe | `completion_time_us` | 7.05331% | [-4.19233, 21.1049] | false | true | inconclusive |
| milestone-16-intel-xe | `ready_wait_p95_us` | 86.771% | [22.7511, 181.028] | true | true | replicated |
| milestone-16-intel-xe | `selection_bypass_max` | 89.4918% | [38.1377, 147.11] | true | true | replicated |
| milestone-16-intel-xe | `cpu_jain_fairness` | 0.255052% | [-0.562056, 1.14141] | false | false | inconclusive |
| milestone-16-intel-xe | `gpu_jain_fairness` | -20.1737% | [-27.1101, -13.8069] | true | true | replicated |
| milestone-16-lavapipe | `response_p95_us` | 48.3043% | [12.1032, 93.4226] | true | true | replicated |
| milestone-16-lavapipe | `completion_time_us` | 2.26352% | [-3.21046, 7.63188] | false | false | inconclusive |
| milestone-16-lavapipe | `ready_wait_p95_us` | 63.5482% | [13.3867, 126.657] | true | true | replicated |
| milestone-16-lavapipe | `selection_bypass_max` | 84.0067% | [34.7726, 139.653] | true | true | replicated |
| milestone-16-lavapipe | `cpu_jain_fairness` | 0.146122% | [-0.345219, 0.612683] | false | false | inconclusive |
| milestone-16-lavapipe | `gpu_jain_fairness` | -21.0576% | [-28.6911, -13.1739] | true | true | replicated |
