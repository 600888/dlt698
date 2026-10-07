import { defineConfig } from '@rspress/core';

const sections = [
  {
    text: '快速开始',
    path: 'getting-started',
    articles: [
      ['installation', '安装与构建'],
      ['quick-start', '第一个程序'],
      ['build-options', '构建选项'],
    ],
  },
  {
    text: '核心类型',
    path: 'core',
    articles: [
      ['result', 'Result 与错误'],
      ['bytes', '字节与缓冲'],
      ['data', 'Data 精确类型'],
      ['codec', 'Data 编解码'],
    ],
  },
  {
    text: '协议层',
    path: 'protocol',
    articles: [
      ['point-model', '对象模型与测点寻址'],
      ['standard-points', '标准固定点位'],
      ['standard-records', '标准记录与能力筛选'],
      ['frame', '链路帧与流解析'],
      ['fragment', '链路分帧'],
      ['apdu', 'APDU 编解码'],
      ['connection', '连接管理 APDU'],
      ['get', 'GET 与记录查询'],
      ['mutation', 'SET 与 ACTION'],
    ],
  },
  {
    text: '会话与服务',
    path: 'session',
    articles: [
        ['server', '托管服务器与设备数据'],
        ['client', '托管客户端'],
      ['session', 'Session 会话'],
      ['service', 'Client/ServerService'],
      ['object', '对象目录与 Provider'],
      ['sync', '同步客户机'],
    ],
  },
  {
    text: '传输层',
    path: 'transport',
    articles: [
      ['executor', '执行器'],
      ['channel', '通道与内存通道'],
      ['tcp', 'TCP 通道'],
      ['serial', '串口与串行链路'],
    ],
  },
  {
    text: '附录',
    path: 'appendix',
    articles: [
      ['error-codes', '错误码参考'],
      ['coverage', '协议覆盖范围'],
      ['examples', '示例程序'],
    ],
  },
];

export default defineConfig({
  root: 'docs',
  base: '/dlt698/',
  siteOrigin: 'https://600888.github.io',
  title: 'dlt698 接口文档',
  description:
    'DL/T 698.45 协议库的对外 C++ 接口文档：核心类型、协议编解码、会话与对象服务、TCP 与串口传输。',
  lang: 'zh',
  icon: '/logo.svg',
  logo: '/logo.svg',
  logoText: 'dlt698',
  route: { cleanUrls: true },
  themeConfig: {
    darkMode: 'light',
    nav: sections.map(({ text, path }) => ({
      text,
      link: `/${path}/`,
      activeMatch: `/${path}/`,
      position: 'left' as const,
    })),
    sidebar: {
      '/': [
        { sectionHeaderText: '开始阅读' },
        { text: '文档首页', link: '/' },
        { text: '关于本文档', link: '/about' },
        { dividerType: 'solid' },
        { sectionHeaderText: '接口模块' },
        ...sections.map(({ text, path }) => ({ text, link: `/${path}/` })),
      ],
      ...Object.fromEntries(
        sections.map(({ text, path, articles }) => [
          `/${path}/`,
          [
            { text: '返回文档首页', link: '/' },
            { dividerType: 'solid' },
            { sectionHeaderText: text },
            { text: '模块导读', link: `/${path}/` },
            {
              text: '接口说明',
              collapsible: true,
              collapsed: false,
              items: articles.map(([slug, label]) => ({
                text: label,
                link: `/${path}/${slug}`,
              })),
            },
          ],
        ]),
      ),
    },
    search: true,
    lastUpdated: true,
    editLink: { docRepoBaseUrl: 'https://github.com/600888/dlt698/tree/main/website/docs' },
    socialLinks: [{ icon: 'github', mode: 'link', content: 'https://github.com/600888/dlt698' }],
    enableScrollToTop: true,
    llmsUI: false,
  },
});
