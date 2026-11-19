from pathlib import Path
p=Path('work/finalize_r16_report.py');s=p.read_text(encoding='utf-8').replace('r16','r17').replace('work/test-r15-sn2-matrix','work/test-r17-secondary-sn2')
s=s.replace('额外 20 个 SN2 R/S 矩阵','额外 20 个二级碳底物 SN2 R/S 矩阵')
s=s.replace('按照用户要求，computer-use 实际界面统一复测暂缓；ChemDraw 实际打开验证此前被用户 Esc 停止。未将自动回归视作已完成这项人工界面验收。','已收到用户睡觉后可进行 computer-use 实测的授权，但工具再次返回“被实体 Esc 停止”，因此没有继续界面操作。ChemDraw 实际打开及统一界面验收尚未完成，不能将自动回归算作这项验收。')
Path('work/finalize_r17_report.py').write_text(s,encoding='utf-8')
