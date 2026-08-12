export interface FileEntry {
    name: string;
    path: string;
    isDirectory: boolean;
    size: number;
    modified: string;
}

export interface DirectoryResult {
    path: string;
    entries: FileEntry[];
}

export interface FileResult {
    path: string;
    size: number;
    content: string;
}

export interface FileError {
    error: string;
}

export interface WriteResult {
    success?: boolean;
    error?: string;
}

export interface UploadResult {
    success: boolean;
    path?: string;
    size?: number;
    error?: string;
}

export interface UploadProgress {
    loaded: number;
    total: number;
    percent: number;
}

export interface OpenTab {
    path: string;
    name: string;
    content: string;
    originalContent: string;
    isDirty: boolean;
}

export function isDirectoryResult(obj: unknown): obj is DirectoryResult {
    return typeof obj === 'object' && obj !== null && 'path' in obj && 'entries' in obj;
}

export function isFileResult(obj: unknown): obj is FileResult {
    return typeof obj === 'object' && obj !== null && 'path' in obj && 'content' in obj;
}

export function isFileError(obj: unknown): obj is FileError {
    return typeof obj === 'object' && obj !== null && 'error' in obj;
}
