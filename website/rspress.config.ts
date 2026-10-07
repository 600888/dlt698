import { defineConfig } from '@rspress/core';

const sections = [
  {
    text: '使用指南',
    path: 'guide',
    articles: [
      ['index', '指南首页'],
      ['first-read', '第一次读取'],
      ['tcp', 'TCP 连接'],
      ['serial', '串口连接'],
      ['addressing', '看懂数据地址'],
      ['configuration', '配置速查'],
      ['energy', '电能读取'],
      ['demand', '需量读取'],
      ['voltage', '电压读取'],
      ['current', '电流读取'],
      ['power', '功率读取'],
      ['power-factor', '功率因数读取'],
      ['frequency-temperature', '频率与温度读取'],
      ['harmonics', '谐波与波形失真度'],
      ['status', '状态字读取'],
      ['clock', '日期时间与校时'],
      ['device-info', '设备信息读取'],
      ['meter-parameters', '电表参数读取与设置'],
      ['daily-freeze', '日冻结查询'],
      ['monthly-freeze', '月冻结查询'],
      ['events', '事件记录查询'],
      ['batch-read', '一次读取多个数据'],
      ['write-action', '写参数与执行方法'],
      ['server', '对外提供设备数据'],
      ['packet-debug', '调试助手使用'],
      ['troubleshooting', '常见问题排查'],
    ],
  },
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
      ['tcp-debug', 'TCP 手动报文调试'],
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
    'DL/T 698.45 协议库的对外 C++ 接口文档：使用指南、核心类型、协议编解码、会话与对象服务、TCP 与串口传输。',
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
        { sectionHeaderText: '文档模块' },
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
              // index 与「模块导读」指向同一页，重复列出会产生两条相同链接
              items: articles
                .filter(([slug]) => slug !== 'index')
                .map(([slug, label]) => ({
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