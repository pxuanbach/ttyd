import { DirectoryResult, FileResult, WriteResult, isFileError } from './types';

const basePath = window.location.pathname.replace(/[/]+$/, '');

export class FileApi {
    private static async request<T>(endpoint: string, method: string = 'GET', body?: object): Promise<T> {
        const url = `${basePath}${endpoint}`;
        const options: RequestInit = {
            method,
            headers: {
                'Content-Type': 'application/json',
            },
        };

        if (body) {
            options.body = JSON.stringify(body);
        }

        const response = await fetch(url, options);
        const data = await response.json();

        if (!response.ok || isFileError(data)) {
            throw new Error(isFileError(data) ? data.error : `HTTP ${response.status}`);
        }

        return data as T;
    }

    static async listDirectory(path?: string): Promise<DirectoryResult> {
        // API: /api/directory or /api/directory/some/path
        const endpoint = path ? `/api/directory/${encodeURIComponent(path)}` : '/api/directory';
        return this.request<DirectoryResult>(endpoint);
    }

    static async readFile(path: string): Promise<FileResult> {
        // API: /api/file/path/to/file.txt
        const endpoint = `/api/file/${encodeURIComponent(path)}`;
        return this.request<FileResult>(endpoint);
    }

    static async writeFile(path: string, content: string): Promise<WriteResult> {
        return this.request<WriteResult>('/api/file', 'POST', { path, content });
    }

    static async deleteFile(path: string): Promise<WriteResult> {
        // API: /api/file/path/to/file.txt
        const endpoint = `/api/file/${encodeURIComponent(path)}`;
        return this.request<WriteResult>(endpoint, 'DELETE');
    }
}
