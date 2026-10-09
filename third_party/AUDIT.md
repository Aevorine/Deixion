# 第三方依赖审计记录

只有两个第三方组件，都来自官方渠道，哈希与官方登记值逐字节核对一致。

| 组件 | 版本 | 来源 | 核验 | 许可证 | 用途 |
|---|---|---|---|---|---|
| Microsoft.Web.WebView2（`WebView2.h`、`WebView2EnvironmentOptions.h`、`WebView2Loader.dll`） | 1.0.4191.47（2026-08-28 发布） | nuget.org，作者 Microsoft | 包 SHA-512 与 NuGet 目录登记值一致；`WebView2Loader.dll` Authenticode 签名有效（Microsoft Corporation）；包内 `.targets` 不被构建使用 | BSD 风格（`webview2/LICENSE.txt`） | 界面宿主 |
| KaTeX（`katex.min.js`、`katex.min.css`、`fonts/*.woff2`） | 0.16.28（2026-01-25 发布） | registry.npmjs.org，Khan Academy | tarball SHA-512 与 `dist.integrity` 一致；只取 `dist` 产物，不执行包内脚本 | MIT（`ui/vendor/katex/LICENSE`） | 公式渲染 |

核验日期：2026-10-08。升级任一组件要重做上面的核验，并更新本表。

其余全部代码（C++ 核心、JavaScript 界面、构建脚本）都是本项目自写，没有其他第三方源码。运行时只依赖 Windows 自带组件（UI Automation、DXGI、WIC、WinHTTP、BCrypt）和系统里的 WebView2 运行时。
