import { DirectoryResult, FileResult, WriteResult, UploadResult, UploadProgress, isFileError } from './types';

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

    /**
     * Upload a file to the specified target directory.
     * Uses XHR for progress tracking.
     * @param targetPath - The directory path to upload to
     * @param file - The file to upload
     * @param onProgress - Optional callback for upload progress
     * @returns UploadResult with the uploaded file path and size
     */
    static uploadFile(
        targetPath: string,
        file: File,
        onProgress?: (progress: UploadProgress) => void
    ): Promise<UploadResult> {
        return new Promise((resolve, reject) => {
            const xhr = new XMLHttpRequest();
            const formData = new FormData();

            // URL: /api/upload?path=<encoded_target_path>
            const url = `${basePath}/api/upload?path=${encodeURIComponent(targetPath)}`;

            xhr.open('POST', url, true);

            // Progress handler
            if (onProgress) {
                xhr.upload.onprogress = e => {
                    if (e.lengthComputable) {
                        onProgress({
                            loaded: e.loaded,
                            total: e.total,
                            percent: Math.round((e.loaded / e.total) * 100),
                        });
                    }
                };
            }

            xhr.onload = () => {
                if (xhr.status >= 200 && xhr.status < 300) {
                    try {
                        const data = JSON.parse(xhr.responseText);
                        if (data.success) {
                            resolve(data as UploadResult);
                        } else {
                            reject(new Error(data.error || 'Upload failed'));
                        }
                    } catch {
                        reject(new Error('Invalid response from server'));
                    }
                } else {
                    try {
                        const data = JSON.parse(xhr.responseText);
                        reject(new Error(data.error || `Upload failed with status ${xhr.status}`));
                    } catch {
                        reject(new Error(`Upload failed with status ${xhr.status}`));
                    }
                }
            };

            xhr.onerror = () => {
                reject(new Error('Network error during upload'));
            };

            xhr.onabort = () => {
                reject(new Error('Upload cancelled'));
            };

            // Add the file to form data with the field name 'file'
            formData.append('file', file);

            // Send the request
            xhr.send(formData);
        });
    }
}
