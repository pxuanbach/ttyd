import { h, Component } from 'preact';
import { DirectoryTree } from './DirectoryTree';
import { FileEditor } from './FileEditor';
import { FileEntry, OpenTab } from './types';
import { FileApi } from './api';

interface Props {
    isOpen: boolean;
    onToggle: () => void;
}

interface State {
    activeTab: string | null;
    tabs: OpenTab[];
    showEditor: boolean;
}

export class FileExplorer extends Component<Props, State> {
    constructor(props: Props) {
        super(props);
        this.state = {
            activeTab: null,
            tabs: [],
            showEditor: false,
        };
    }

    handleOpenFile = async (entry: FileEntry) => {
        // Check if file is already open
        const existingTab = this.state.tabs.find(t => t.path === entry.path);
        if (existingTab) {
            this.setState({ activeTab: entry.path, showEditor: true });
            return;
        }

        try {
            const result = await FileApi.readFile(entry.path);
            const newTab: OpenTab = {
                path: entry.path,
                name: entry.name,
                content: result.content,
                originalContent: result.content,
                isDirty: false,
            };

            this.setState(
                prev => ({
                    tabs: [...prev.tabs, newTab],
                    activeTab: entry.path,
                    showEditor: true,
                }),
                () => {
                    // Notify terminal to resize
                    this.notifyResize();
                }
            );
        } catch (err) {
            alert(`Failed to open file: ${err instanceof Error ? err.message : err}`);
        }
    };

    handleSelectTab = (path: string) => {
        this.setState({ activeTab: path });
        this.notifyResize();
    };

    handleCloseTab = (path: string) => {
        const tab = this.state.tabs.find(t => t.path === path);
        if (tab?.isDirty) {
            const confirm = window.confirm(`Save changes to ${tab.name}?`);
            if (confirm) {
                this.handleSaveAndClose(path);
                return;
            }
        }

        this.closeTab(path);
    };

    handleSaveAndClose = async (path: string) => {
        const tab = this.state.tabs.find(t => t.path === path);
        if (!tab) return;

        try {
            await FileApi.writeFile(path, tab.content);
            this.closeTab(path);
        } catch (err) {
            alert(`Failed to save: ${err instanceof Error ? err.message : err}`);
        }
    };

    closeTab = (path: string) => {
        this.setState(
            prev => {
                const newTabs = prev.tabs.filter(t => t.path !== path);
                let newActiveTab = prev.activeTab;
                if (prev.activeTab === path) {
                    const closedIndex = prev.tabs.findIndex(t => t.path === path);
                    if (newTabs.length > 0) {
                        newActiveTab = newTabs[Math.min(closedIndex, newTabs.length - 1)].path;
                    } else {
                        newActiveTab = null;
                    }
                }
                return {
                    tabs: newTabs,
                    activeTab: newActiveTab,
                    showEditor: newTabs.length > 0,
                };
            },
            () => {
                this.notifyResize();
            }
        );
    };

    handleTabContentChange = (path: string, content: string) => {
        this.setState(prev => ({
            tabs: prev.tabs.map(t => (t.path === path ? { ...t, content, isDirty: content !== t.originalContent } : t)),
        }));
    };

    toggleEditor = () => {
        this.setState(
            prev => ({ showEditor: !prev.showEditor }),
            () => {
                this.notifyResize();
            }
        );
    };

    notifyResize = () => {
        // Dispatch a custom event to notify terminal to resize
        const event = new CustomEvent('fileExplorerResize', {
            detail: {
                hasEditor: this.state.tabs.length > 0 && this.state.showEditor,
            },
        });
        window.dispatchEvent(event);
    };

    render() {
        const { isOpen, onToggle } = this.props;
        const { tabs, activeTab, showEditor } = this.state;

        if (!isOpen) {
            return (
                <button class="file-explorer-toggle closed" onClick={onToggle} title="Open File Explorer">
                    📁
                </button>
            );
        }

        return (
            <div class="file-explorer">
                <div class="explorer-header">
                    <span class="explorer-title">File Explorer</span>
                    <div class="explorer-actions">
                        <button
                            class={`explorer-btn ${showEditor ? 'active' : ''}`}
                            onClick={this.toggleEditor}
                            title={showEditor ? 'Hide Editor' : 'Show Editor'}
                        >
                            📝
                        </button>
                        <button class="explorer-btn" onClick={onToggle} title="Close">
                            ✕
                        </button>
                    </div>
                </div>

                <div class="explorer-body">
                    <div class="explorer-tree">
                        <DirectoryTree onOpenFile={this.handleOpenFile} />
                    </div>

                    {tabs.length > 0 && showEditor && (
                        <div class="explorer-editor">
                            <FileEditor
                                tabs={tabs}
                                activeTab={activeTab}
                                onSelectTab={this.handleSelectTab}
                                onCloseTab={this.handleCloseTab}
                                onTabContentChange={this.handleTabContentChange}
                                onOpenFile={this.handleOpenFile}
                            />
                        </div>
                    )}
                </div>
            </div>
        );
    }
}
