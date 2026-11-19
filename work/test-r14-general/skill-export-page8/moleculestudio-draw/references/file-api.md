# Core 文件绘图接口

`draw.py --app 安装目录 --input drawing.json --output drawing.cdxml [--overwrite]`

输入顶层只支持 `molecules`、`texts`、`arrows` 三个数组，均可省略但至少有一个对象。未知字段报错。

- molecules：每项 `smiles` 或 `name` 二选一；`name` 为 OPSIN 支持的英文名称，不做网络检索；可选 `x`、`y`（分子几何中心，默认 100,100）、`label`（下方普通文字）。每分子 1–400 原子，最多 100 个分子。不同分子不会自动避让，按返回 bounds 调整。
- texts：`text` 必填，`x`、`y` 为基线位置，默认 100,100；`size` 默认 10，范围 4–96 pt。
- arrows：`x`、`y` 起点，`x2`、`y2` 终点，全部必填；可选 bend（弧高/弦长，-2 到 2，默认 0；正值在屏幕顺时针法线一侧）、fishhook（单电子，默认 false）、head（箭头长度 1–24 pt）、width（线宽 0.1–12 pt，默认 0.6）。机理箭头是原生可编辑对象。文件接口按指定弧度绘制，不自动避障。
- 坐标为有限数值，范围 ±200000；默认键长 30 pt。当前文件接口不提供颜色、上下标或编辑旧文档，不要静默忽略这些需求。

例如用户明确给出乙醇与乙醛 SMILES 后，可以生成：
```json
{
  "molecules": [
    {"smiles": "CCO", "x": 110, "y": 110, "label": "Ethanol"},
    {"smiles": "CC=O", "x": 330, "y": 110, "label": "Acetaldehyde"}
  ],
  "arrows": [{"x": 180, "y": 110, "x2": 260, "y2": 110}],
  "texts": [{"text": "[O]", "x": 205, "y": 90}]
}
```
成功：退出码 0，stdout JSON 含 output、molecules、bounds。失败：非零退出码，stderr JSON 含 error；不写入不完整的 CDXML。
输出可在 MoleculeStudio 中打开并继续编辑。文件生成不是反应正确性校验；例子只是排版演示，不含实验条件。
