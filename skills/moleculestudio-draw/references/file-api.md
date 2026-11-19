# Core 文件绘图接口

`draw.py --app 安装目录 --input drawing.json --output drawing.cdxml [--overwrite]`

简单绘图格式支持 `molecules`、`texts`、`arrows` 三个数组，均可省略但至少有一个对象。未知字段报错。

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

## r51 原生工具计划

顶层使用 `tools` 数组时，每项是 `{"tool":"函数名","args":{...}}`。支持结构、试剂库、电子标记、机理箭头、协同推演、过渡态和催化循环。完整参数见 AI_TOOL_REFERENCE.json。工具调用按顺序执行，原生检查通过后输出可编辑 CDXML。源文件用 `--source initial.cdxml`，新建文档时省略。先执行 `draw.py --app 安装目录 --inspect --source initial.cdxml` 读取当前引用。处理后续步骤时再次检查上一步输出。跨文件时重新读取引用。

以下示例从空白页生成亲核取代的箭头和产物。new:1 的子引用来自本例 CBr 的结构记录，其他结构应先查询实际引用。

```json
{
  "tools": [
    {
      "tool": "draw_smiles",
      "args": {
        "smiles": "CBr",
        "x": 55,
        "y": 50
      }
    },
    {
      "tool": "add_mechanism_reagent",
      "args": {
        "role": "nucleophile",
        "x": 15,
        "y": 50
      }
    },
    {
      "tool": "draw_mechanism_step",
      "args": {
        "moves": [
          {
            "source": "new:2",
            "target": "new:1/atom:10"
          },
          {
            "source": "new:1/bond:1000",
            "target": "new:1/atom:11"
          }
        ]
      }
    }
  ]
}
```

## 多步机理排版（r52）

使用 `tools` 调用 `draw_scheme`，每步给出 `reactants`、`products` 和可选的 `electron_moves`。文件接口使用 `smiles:` 结构。电子端点格式为“反应物序号:原子序号”或“反应物序号:原子序号-原子序号”，序号从 1 开始。独立的 [H] 原子可参与转移。以羟离子和溴甲烷为例，两支箭头可以这样写：

```json
{"reactants":["smiles:[OH-]","smiles:CBr"],"products":["smiles:CO","smiles:[Br-]"],"electron_moves":[{"source":"1:1","target":"2:1"},{"source":"2:1-2","target":"2:2"}]}
```

经典羧酸型 Mitsunobu 酯化可用一个离线工具调用，生成四行可编辑机理。其他反应可在反应物 SMILES 上使用原子映射号，在电子箭头端点用 `1:@7` 或 `2:@3-@4` 引用。

```json
{"tools":[{"tool":"draw_mitsunobu","args":{"alcohol":"CC(O)C","acid":"CC(=O)O"}}]}
```
