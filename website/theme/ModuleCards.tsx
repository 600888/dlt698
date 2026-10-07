import { Link } from '@rspress/core/theme-original';

const modules = [
  {
    number: '01',
    label: '使用指南(C++)',
    en: 'C++ GUIDE',
    href: '/guide/',
    description: '连接设备、读取点位、理解结果，按业务场景查找详细用法。',
    tags: 'Client · Server · 点位与报文',
    icon: 'book',
  },
  {
    number: '02',
    label: '使用指南(Python)',
    en: 'PYTHON GUIDE',
    href: '/python/',
    description: '安装后直接读取，代码示例覆盖同步、异步及常用操作。',
    tags: 'pip · Data · asyncio',
    icon: 'book',
  },
  {
    number: '03',
    label: '快速开始',
    en: 'GETTING STARTED',
    href: '/getting-started/',
    description: '从安装、构建到第一个可运行程序，先把库用起来。',
    tags: 'CMake · 安装导出 · 示例',
    icon: 'rocket',
  },
  {
    number: '04',
    label: '核心类型',
    en: 'CORE TYPES',
    href: '/core/',
    description: 'Result 错误模型、字节缓冲与保留协议标签的精确类型 Data。',
    tags: 'Result · ByteView · Data · A-XDR',
    icon: 'cube',
  },
  {
    number: '05',
    label: '协议层',
    en: 'PROTOCOL',
    href: '/protocol/',
    description: '链路帧、流解析、链路分帧，以及各类 APDU 的编解码入口。',
    tags: 'Frame · Fragment · APDU',
    icon: 'network',
  },
  {
    number: '06',
    label: '会话与服务',
    en: 'SESSION & SERVICE',
    href: '/session/',
    description: '连接与事务管理、对象读写与方法分发，以及同步等待适配。',
    tags: 'Session · ObjectRegistry · Sync',
    icon: 'link',
  },
  {
    number: '07',
    label: '传输层',
    en: 'TRANSPORT',
    href: '/transport/',
    description: '执行器模型、内存通道、TCP 通道，以及串口与 698 串行时序。',
    tags: 'Executor · TCP · RS-485',
    icon: 'plug',
  },
  {
    number: '08',
    label: '附录',
    en: 'APPENDIX',
    href: '/appendix/',
    description: '错误码参考、协议覆盖范围与示例程序说明。',
    tags: '错误码 · 覆盖矩阵 · 示例',
    icon: 'book',
  },
];

function ModuleIcon({ name }: { name: string }) {
  const paths: Record<string, React.ReactNode> = {
    rocket: (
      <>
        <path d="M12 3c3 2 5 5.5 5 9l-2.5 2.5h-5L7 12c0-3.5 2-7 5-9Z" />
        <path d="M9.5 14.5 7 17l3-1 1-1.5ZM14.5 14.5 17 17l-3-1-1-1.5ZM10.5 18.5 9 21l2.5-1 .5-1.5Z" />
      </>
    ),
    cube: (
      <>
        <rect x="4" y="4" width="16" height="16" rx="2.5" />
        <path d="M4 10h16M10 4v16" />
      </>
    ),
    network: (
      <>
        <circle cx="12" cy="12" r="9" />
        <ellipse cx="12" cy="12" rx="4" ry="9" />
        <path d="M3 12h18M5 6h14M5 18h14" />
      </>
    ),
    link: (
      <>
        <path d="M10 13a4 4 0 0 0 5.7.4l2.6-2.6a4 4 0 0 0-5.7-5.7L11.2 6.6" />
        <path d="M14 11a4 4 0 0 0-5.7-.4L5.7 13.2a4 4 0 0 0 5.7 5.7l1.4-1.4" />
      </>
    ),
    plug: (
      <>
        <path d="M9 3v6M15 3v6" />
        <path d="M7 9h10v3a5 5 0 0 1-10 0V9Z" />
        <path d="M12 17v4" />
      </>
    ),
    book: (
      <>
        <path d="M4 5.5A2.5 2.5 0 0 1 6.5 3H19v15H6.5A2.5 2.5 0 0 0 4 20.5V5.5Z" />
        <path d="M4 20.5A2.5 2.5 0 0 1 6.5 18H19v3H6.5" />
      </>
    ),
  };
  return (
    <svg
      viewBox="0 0 24 24"
      width="25"
      height="25"
      fill="none"
      stroke="currentColor"
      strokeWidth="1.5"
      strokeLinecap="round"
      strokeLinejoin="round"
      aria-hidden="true"
    >
      {paths[name]}
    </svg>
  );
}

export function ModuleCards() {
  return (
    <div className="module-grid">
      {modules.map(module => (
        <Link key={module.href} href={module.href} className="module-card">
          <div className="module-card-top">
            <span className="module-icon">
              <ModuleIcon name={module.icon} />
            </span>
            <span className="module-number">{module.number}</span>
          </div>
          <span className="module-en">{module.en}</span>
          <strong className="module-title">
            {module.label}
            <span aria-hidden="true">↗</span>
          </strong>
          <span className="module-description">{module.description}</span>
          <span className="module-tags">{module.tags}</span>
        </Link>
      ))}
    </div>
  );
}
