import { h, Component, Fragment } from 'preact';
import { Panel, Group, Separator } from 'react-resizable-panels';

import { Terminal } from './terminal';
import { FileExplorer } from './fileExplorer';
import { ToastContainer } from './common/Toast';

import type { ITerminalOptions, ITheme } from '@xterm/xterm';
import type { ClientOptions, FlowControl } from './terminal/xterm';

const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
const path = window.location.pathname.replace(/[/]+$/, '');
const wsUrl = [protocol, '//', window.location.host, path, '/ws', window.location.search].join('');
const tokenUrl = [window.location.protocol, '//', window.location.host, path, '/token'].join('');
const clientOptions = {
    rendererType: 'webgl',
    disableLeaveAlert: false,
    disableResizeOverlay: false,
    enableZmodem: false,
    enableTrzsz: false,
    enableSixel: false,
    closeOnDisconnect: false,
    isWindows: false,
    unicodeVersion: '11',
} as ClientOptions;
const termOptions = {
    fontSize: 13,
    fontFamily: 'Consolas,Liberation Mono,Menlo,Courier,monospace',
    theme: {
        foreground: '#d2d2d2',
        background: '#2b2b2b',
        cursor: '#adadad',
        black: '#000000',
        red: '#d81e00',
        green: '#5ea702',
        yellow: '#cfae00',
        blue: '#427ab3',
        magenta: '#89658e',
        cyan: '#00a7aa',
        white: '#dbded8',
        brightBlack: '#686a66',
        brightRed: '#f54235',
        brightGreen: '#99e343',
        brightYellow: '#fdeb61',
        brightBlue: '#84b0d8',
        brightMagenta: '#bc94b7',
        brightCyan: '#37e6e8',
        brightWhite: '#f1f1f0',
    } as ITheme,
    allowProposedApi: true,
} as ITerminalOptions;
const flowControl = {
    limit: 100000,
    highWater: 10,
    lowWater: 4,
} as FlowControl;

interface State {
    fileExplorerOpen: boolean;
}

export class App extends Component<{}, State> {
    constructor(props: {}) {
        super(props);
        this.state = {
            fileExplorerOpen: false,
        };
    }

    toggleFileExplorer = () => {
        this.setState(prev => ({
            fileExplorerOpen: !prev.fileExplorerOpen,
        }));
    };

    render(_props: {}, { fileExplorerOpen }: State) {
        return (
            <div id="app-container">
                {!fileExplorerOpen && (
                    <button
                        class="file-explorer-toggle closed"
                        onClick={this.toggleFileExplorer}
                        title="Open File Explorer"
                    >
                        📁
                    </button>
                )}
                <Group orientation="horizontal" id="main-panel-group">
                    <Panel id="terminal-panel" defaultSize={fileExplorerOpen ? '70' : '100'} minSize="20">
                        <Terminal
                            id="terminal-container"
                            wsUrl={wsUrl}
                            tokenUrl={tokenUrl}
                            clientOptions={clientOptions}
                            termOptions={termOptions}
                            flowControl={flowControl}
                        />
                    </Panel>

                    {fileExplorerOpen && (
                        <Fragment>
                            <Separator id="sidebar-resize-handle" className="resize-handle horizontal" />
                            <Panel
                                id="file-explorer-panel"
                                defaultSize="30"
                                minSize="10"
                                maxSize="70"
                                storageKey="ttyd-file-explorer-width"
                            >
                                <FileExplorer isOpen={fileExplorerOpen} onToggle={this.toggleFileExplorer} />
                            </Panel>
                        </Fragment>
                    )}
                </Group>

                {/* Toast notifications */}
                <ToastContainer />
            </div>
        );
    }
}
