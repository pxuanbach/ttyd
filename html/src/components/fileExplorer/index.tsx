import { h, Component, Fragment } from 'preact';
import { Panel, Group, Separator } from 'react-resizable-panels';
import { DirectoryTree } from './DirectoryTree';
import { FileEditor } from './FileEditor';
import { ContextMenu, ContextMenuItem } from './ContextMenu';
import { FileEntry, OpenTab, UploadProgress, isImageFile } from './types';
import { FileApi } from './api';

const EDITOR_STORAGE_KEY = 'ttyd-file-editor-height';

interface Props {
    isOpen: boolean;
    onToggle: () => void;
}

interface ContextMenuState {
    x: number;
    y: number;
    entry: FileEntry;
}

interface State {
    activeTab: string | null;
    tabs: OpenTab[];
    showEditor: boolean;
    contextMenu: ContextMenuState | null;
    uploadProgress: UploadProgress | null;
}

export class FileExplorer extends Component<Props, State> {
    constructor(props: Props) {
        super(props);
        this.state = {
            activeTab: null,
            tabs: [],
            showEditor: false,
            contextMenu: null,
            uploadProgress: null,
        };
    }

    handleOpenFile = async (entry: FileEntry) => {
        // Check if file is already open
        const existingTab = this.state.tabs.find(t => t.path === entry.path);
        if (existingTab) {
            this.setState({ activeTab: entry.path, showEditor: true });
            return;
        }

        const kind = isImageFile(entry.name) ? 'image' : 'text';

        if (kind === 'image') {
            // Skip readFile() for images - backend serves bytes via /api/image/{path} directly.
            const newTab: OpenTab = {
                path: entry.path,
                name: entry.name,
                content: '',
                originalContent: '',
                isDirty: false,
                kind: 'image',
            };
            this.setState(
                prev => ({
                    tabs: [...prev.tabs, newTab],
                    activeTab: entry.path,
                    showEditor: true,
                }),
                () => this.notifyResize()
            );
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
                kind: 'text',
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

    handleContextMenu = (entry: FileEntry, x: number, y: number) => {
        this.setState({
            contextMenu: { x, y, entry },
        });
    };

    closeContextMenu = () => {
        this.setState({ contextMenu: null });
    };

    handleContextMenuAction = async (action: string) => {
        const { contextMenu } = this.state;
        if (!contextMenu) return;

        const { entry } = contextMenu;

        switch (action) {
            case 'upload':
                this.handleUploadFiles(entry.path);
                return; // handleUploadFiles handles closing context menu
            case 'refresh':
                // Trigger a refresh by reloading the current directory
                // This will be handled by DirectoryTree
                window.dispatchEvent(new CustomEvent('fileExplorerRefresh'));
                break;
            case 'newFile':
                this.handleNewFile(entry.path);
                break;
            case 'newFolder':
                this.handleNewFolder();
                break;
        }

        this.closeContextMenu();
    };

    handleUploadFiles = (targetPath: string) => {
        // Close context menu first
        this.closeContextMenu();

        // Use setTimeout to ensure context menu is fully closed before showing file picker
        setTimeout(() => {
            const input = document.createElement('input');
            input.type = 'file';
            input.multiple = true;
            input.style.display = 'none';

            input.onchange = async () => {
                const files = input.files;
                if (!files || files.length === 0) {
                    input.remove();
                    return;
                }

                let successCount = 0;
                let failCount = 0;

                for (const file of Array.from(files)) {
                    try {
                        await FileApi.uploadFile(targetPath, file, progress => {
                            this.setState({ uploadProgress: progress });
                        });
                        successCount++;
                    } catch (err) {
                        console.error(`Failed to upload ${file.name}:`, err);
                        failCount++;
                    }
                }

                this.setState({ uploadProgress: null });

                // Show result notification
                if (successCount > 0 && failCount === 0) {
                    window.dispatchEvent(
                        new CustomEvent('showToast', {
                            detail: { message: `Uploaded ${successCount} file(s) successfully`, type: 'success' },
                        })
                    );
                } else if (successCount > 0 && failCount > 0) {
                    window.dispatchEvent(
                        new CustomEvent('showToast', {
                            detail: { message: `Uploaded ${successCount} files, ${failCount} failed`, type: 'warning' },
                        })
                    );
                } else if (failCount > 0) {
                    window.dispatchEvent(
                        new CustomEvent('showToast', {
                            detail: { message: `Failed to upload ${failCount} file(s)`, type: 'error' },
                        })
                    );
                }

                // Refresh the directory to show new files
                window.dispatchEvent(new CustomEvent('fileExplorerRefresh'));

                // Clean up
                input.remove();
            };

            input.oncancel = () => {
                input.remove();
            };

            // Append to body and trigger file picker
            document.body.appendChild(input);
            input.click();
        }, 0);
    };

    handleNewFile = async (dirPath: string) => {
        const name = window.prompt('Enter file name:');
        if (!name) return;

        const filePath = dirPath.endsWith('/') ? `${dirPath}${name}` : `${dirPath}/${name}`;

        try {
            await FileApi.writeFile(filePath, '');
            window.dispatchEvent(new CustomEvent('fileExplorerRefresh'));
            window.dispatchEvent(
                new CustomEvent('showToast', {
                    detail: { message: `Created ${name}`, type: 'success' },
                })
            );
        } catch (err) {
            window.dispatchEvent(
                new CustomEvent('showToast', {
                    detail: {
                        message: `Failed to create file: ${err instanceof Error ? err.message : err}`,
                        type: 'error',
                    },
                })
            );
        }
    };

    handleNewFolder = async () => {
        const name = window.prompt('Enter folder name:');
        if (!name) return;

        // For now, creating a folder would need a new API endpoint
        // For simplicity, we'll just show a toast
        window.dispatchEvent(
            new CustomEvent('showToast', {
                detail: { message: 'Creating folders via web UI is not yet implemented', type: 'info' },
            })
        );
    };

    getContextMenuItems = (entry: FileEntry): ContextMenuItem[] => {
        const items: ContextMenuItem[] = [];

        if (entry.isDirectory) {
            items.push({
                id: 'upload',
                label: 'Upload Files',
            });
            items.push({ id: 'divider-upload', label: '', divider: true });
        }

        items.push({
            id: 'newFile',
            label: 'New File',
        });

        items.push({
            id: 'newFolder',
            label: 'New Folder',
        });

        items.push({ id: 'divider-refresh', label: '', divider: true });

        items.push({
            id: 'refresh',
            label: 'Refresh',
        });

        return items;
    };

    render() {
        const { isOpen, onToggle } = this.props;
        const { tabs, activeTab, showEditor, contextMenu, uploadProgress } = this.state;

        // When not open, FileExplorer is not rendered (handled by parent App component)
        if (!isOpen) {
            return null;
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
                    <Group orientation="vertical" id="editor-panel-group">
                        <Panel
                            id="directory-tree-panel"
                            defaultSize={showEditor && tabs.length > 0 ? '50' : '100'}
                            minSize="20"
                        >
                            <div class="explorer-tree">
                                <DirectoryTree
                                    onOpenFile={this.handleOpenFile}
                                    onContextMenu={this.handleContextMenu}
                                />
                            </div>
                        </Panel>

                        {tabs.length > 0 && showEditor && (
                            <Fragment>
                                <Separator id="editor-resize-handle" className="resize-handle vertical" />
                                <Panel
                                    id="file-editor-panel"
                                    defaultSize="50"
                                    minSize="15"
                                    maxSize="70"
                                    storageKey={EDITOR_STORAGE_KEY}
                                >
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
                                </Panel>
                            </Fragment>
                        )}
                    </Group>
                </div>

                {/* Context Menu */}
                {contextMenu && (
                    <ContextMenu
                        x={contextMenu.x}
                        y={contextMenu.y}
                        items={this.getContextMenuItems(contextMenu.entry)}
                        onSelect={this.handleContextMenuAction}
                        onClose={this.closeContextMenu}
                    />
                )}

                {/* Upload Progress Indicator */}
                {uploadProgress && (
                    <div class="upload-progress">
                        <div class="upload-progress__bar" style={{ width: `${uploadProgress.percent}%` }} />
                        <span class="upload-progress__text">Uploading... {uploadProgress.percent}%</span>
                    </div>
                )}
            </div>
        );
    }
}
