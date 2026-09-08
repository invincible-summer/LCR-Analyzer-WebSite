# 四组实测的 v4 先验文件

CSV 和电路说明仍在上一级。这里提供原格式的元件/拓扑输入，可直接运行：

```sh
/tmp/lcr-v4-build/lcr try2 --csv examples/data2.csv --components examples/v4/data2.components.txt
/tmp/lcr-v4-build/lcr try2 --csv examples/data2.csv --components examples/v4/data2.components.txt --tolerance .1
/tmp/lcr-v4-build/lcr try3 --csv examples/data2.csv --topology examples/v4/data2.topology.txt --json
```

元件文件为标称值；Exact 的残差不等于使用实测回收值的最小残差。
Tolerance 需要明确选择物理上可信的容差，不能仅为降低误差而无限放宽。
