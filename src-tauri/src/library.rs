use std::collections::HashSet;
use std::fs::{self, Metadata};
use std::path::{Path, PathBuf};
use std::sync::{Mutex, OnceLock};

#[cfg(windows)]
use std::fs::{File, OpenOptions};
#[cfg(windows)]
use std::os::windows::fs::{MetadataExt, OpenOptionsExt};

use rusqlite::{params, Connection, OptionalExtension};
use serde::{Deserialize, Serialize};
#[cfg(not(test))]
use tauri::State;

#[cfg(test)]
use crate::backends::host_state::ClipRecord;
#[cfg(not(test))]
use crate::state::ClipRecord;

const LIBRARY_SCHEMA_VERSION: i32 = 2;

static REGISTERED_ROOTS: OnceLock<Mutex<Vec<PathBuf>>> = OnceLock::new();

#[cfg(windows)]
const FILE_SHARE_READ: u32 = 0x0000_0001;
#[cfg(windows)]
const FILE_SHARE_WRITE: u32 = 0x0000_0002;
#[cfg(windows)]
const FILE_SHARE_DELETE: u32 = 0x0000_0004;
#[cfg(windows)]
const FILE_READ_ATTRIBUTES: u32 = 0x0000_0080;
#[cfg(windows)]
const DELETE_ACCESS: u32 = 0x0001_0000;
#[cfg(windows)]
const FILE_FLAG_BACKUP_SEMANTICS: u32 = 0x0200_0000;
#[cfg(windows)]
const FILE_FLAG_OPEN_REPARSE_POINT: u32 = 0x0020_0000;
#[cfg(windows)]
const FILE_FLAG_DELETE_ON_CLOSE: u32 = 0x0400_0000;

#[derive(Clone, Debug, Deserialize, Serialize)]
#[serde(rename_all = "camelCase")]
pub struct ClipMetadata {
    pub id: String,
    pub title: String,
    pub path: String,
    pub created_at_ms: u64,
    pub duration_seconds: u32,
    pub size_bytes: u64,
    pub codec: String,
    pub format: String,
    pub width: Option<u32>,
    pub height: Option<u32>,
    pub fps: Option<u32>,
    pub has_audio: bool,
    pub tags: Vec<String>,
    pub favorite: bool,
    pub proxy_path: Option<String>,
    pub proxy_status: String,
    pub file_status: String,
}

#[derive(Clone, Debug, Deserialize)]
#[serde(rename_all = "camelCase")]
pub struct ClipUpdate {
    pub title: Option<String>,
    pub tags: Option<Vec<String>>,
    pub favorite: Option<bool>,
}

pub struct LibraryStore {
    connection: Connection,
}

/// Keeps every directory between a registered root and a target open while a
/// path operation is in progress. Windows does not expose a safe relative
/// delete primitive through `std`, but denying directory deletion/rename while
/// opening the final object closes the useful reparse-point swap window.
pub(crate) struct RegisteredDirectoryGuard {
    #[cfg(windows)]
    handles: Vec<File>,
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum FileState {
    Present,
    Missing,
    Unsafe,
}

impl LibraryStore {
    pub fn open(path: &Path) -> Result<Self, String> {
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent).map_err(|error| error.to_string())?;
        }
        let mut connection = Connection::open(path).map_err(|error| error.to_string())?;
        connection
            .pragma_update(None, "journal_mode", "wal")
            .map_err(|error| error.to_string())?;
        connection
            .busy_timeout(std::time::Duration::from_secs(2))
            .map_err(|error| error.to_string())?;
        migrate(&mut connection)?;
        let store = Self { connection };
        store.reconcile()?;
        Ok(store)
    }

    pub fn insert_record(&self, record: &ClipRecord) -> Result<(), String> {
        let path = safe_existing_file(Path::new(&record.path))?;
        let proxy_path = record
            .proxy_path
            .as_deref()
            .map(Path::new)
            .map(safe_existing_file)
            .transpose()?;
        let title = path
            .file_stem()
            .and_then(|value| value.to_str())
            .unwrap_or("MoonLit clip")
            .to_string();
        let tags =
            serde_json::to_string(&Vec::<String>::new()).map_err(|error| error.to_string())?;
        self.connection
            .execute(
                "INSERT INTO clips (id,title,path,created_at_ms,duration_seconds,size_bytes,codec,format,width,height,fps,has_audio,tags_json,favorite,proxy_path,proxy_status,file_status)
                 VALUES (?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11,?12,?13,?14,?15,?16,?17)
                 ON CONFLICT(id) DO UPDATE SET path=excluded.path, file_status=excluded.file_status",
                params![
                    record.id,
                    title,
                    path.to_string_lossy().into_owned(),
                    saturating_i64(record.created_at_ms),
                    saturating_i64(record.duration_seconds as u64),
                    saturating_i64(record.size_bytes),
                    record.codec,
                    record.format,
                    record.width.map(|value| value as i64),
                    record.height.map(|value| value as i64),
                    record.fps.map(|value| value as i64),
                    record.has_audio,
                    tags,
                    false,
                    proxy_path.map(|path| path.to_string_lossy().into_owned()),
                    record.proxy_status,
                    "present",
                ],
            )
            .map_err(|error| error.to_string())?;
        Ok(())
    }

    pub(crate) fn remove_unindexed_file(&self, path: &Path) -> Result<(), String> {
        let owned: bool = self
            .connection
            .query_row(
                "SELECT EXISTS(SELECT 1 FROM clips WHERE path = ?1)",
                params![path.to_string_lossy().into_owned()],
                |row| row.get(0),
            )
            .map_err(|error| error.to_string())?;
        if owned {
            return Ok(());
        }
        remove_registered_file(path)
    }

    pub fn list(&self, query: Option<&str>) -> Result<Vec<ClipMetadata>, String> {
        self.list_page(query, 500, 0)
    }

    pub fn list_page(
        &self,
        query: Option<&str>,
        limit: u32,
        offset: u32,
    ) -> Result<Vec<ClipMetadata>, String> {
        let limit = i64::from(limit.clamp(1, 500));
        let offset = i64::from(offset);
        let mut statement = self
            .connection
            .prepare(
                "SELECT id,title,path,created_at_ms,duration_seconds,size_bytes,codec,format,width,height,fps,has_audio,tags_json,favorite,proxy_path,proxy_status,file_status
                 FROM clips WHERE (?1 IS NULL OR title LIKE '%' || ?1 || '%' OR tags_json LIKE '%' || ?1 || '%')
                 ORDER BY created_at_ms DESC, id DESC LIMIT ?2 OFFSET ?3",
            )
            .map_err(|error| error.to_string())?;
        let rows = statement
            .query_map(params![query, limit, offset], map_row)
            .map_err(|error| error.to_string())?;
        rows.map(|row| row.map_err(|error| error.to_string()))
            .collect()
    }

    pub fn get(&self, id: &str) -> Result<Option<ClipMetadata>, String> {
        self.connection
            .query_row(
                "SELECT id,title,path,created_at_ms,duration_seconds,size_bytes,codec,format,width,height,fps,has_audio,tags_json,favorite,proxy_path,proxy_status,file_status FROM clips WHERE id = ?1",
                params![id],
                map_row,
            )
            .optional()
            .map_err(|error| error.to_string())
    }

    pub fn update(&self, id: &str, update: ClipUpdate) -> Result<(), String> {
        let current = self
            .get(id)?
            .ok_or_else(|| "Clip no encontrado".to_string())?;
        let title = update.title.unwrap_or(current.title);
        let tags = serde_json::to_string(&update.tags.unwrap_or(current.tags))
            .map_err(|error| error.to_string())?;
        let favorite = update.favorite.unwrap_or(current.favorite);
        self.connection
            .execute(
                "UPDATE clips SET title = ?1, tags_json = ?2, favorite = ?3 WHERE id = ?4",
                params![title, tags, favorite, id],
            )
            .map_err(|error| error.to_string())?;
        Ok(())
    }

    pub fn delete(&self, id: &str) -> Result<(), String> {
        let Some(clip) = self.get(id)? else {
            return Ok(());
        };

        // Validate every path before removing any file. A forged proxy path must
        // never cause the real clip to be removed first.
        let mut targets = Vec::new();
        if let Some(path) = safe_deletion_target(Path::new(&clip.path))? {
            targets.push(path);
        }
        if let Some(proxy) = clip.proxy_path.as_deref() {
            if let Some(path) = safe_deletion_target(Path::new(proxy))? {
                if !targets.iter().any(|existing| existing == &path) {
                    targets.push(path);
                }
            }
        }

        for path in targets {
            remove_file_safely(&path)?;
        }
        self.connection
            .execute("DELETE FROM clips WHERE id = ?1", params![id])
            .map_err(|error| error.to_string())?;
        Ok(())
    }

    pub fn set_proxy(&self, id: &str, path: Option<&Path>, status: &str) -> Result<(), String> {
        if !matches!(
            status,
            "notNeeded" | "processing" | "ready" | "failed" | "missing" | "unsafe"
        ) {
            return Err("Estado de proxy no reconocido".to_string());
        }
        if status == "ready" && path.is_none() {
            return Err("Un proxy listo debe tener una ruta".to_string());
        }
        let path = path.map(safe_existing_file).transpose()?;
        let changed = self
            .connection
            .execute(
                "UPDATE clips SET proxy_path = ?1, proxy_status = ?2 WHERE id = ?3",
                params![
                    path.map(|value| value.to_string_lossy().into_owned()),
                    status,
                    id
                ],
            )
            .map_err(|error| error.to_string())?;
        if changed == 0 {
            return Err("Clip no encontrado".to_string());
        }
        Ok(())
    }

    pub fn reconcile(&self) -> Result<u64, String> {
        let mut statement = self
            .connection
            .prepare("SELECT id,path,proxy_path,proxy_status FROM clips")
            .map_err(|error| error.to_string())?;
        let rows = statement
            .query_map([], |row| {
                Ok((
                    row.get::<_, String>(0)?,
                    row.get::<_, String>(1)?,
                    row.get::<_, Option<String>>(2)?,
                    row.get::<_, String>(3)?,
                ))
            })
            .map_err(|error| error.to_string())?;
        let records: Vec<_> = rows
            .map(|row| row.map_err(|error| error.to_string()))
            .collect::<Result<_, _>>()?;
        drop(statement);

        let transaction = self
            .connection
            .unchecked_transaction()
            .map_err(|error| error.to_string())?;
        let mut changed = 0;
        for (id, path, proxy_path, proxy_status) in records {
            let file_status = match classify_file(&path) {
                FileState::Present => "present",
                FileState::Missing => "missing",
                FileState::Unsafe => "unsafe",
            };
            let next_proxy_status = match proxy_path.as_deref() {
                Some(path) if classify_file(path) == FileState::Present => "ready",
                Some(_) => "failed",
                None if proxy_status == "processing" => "failed",
                None => proxy_status.as_str(),
            };
            let updated = transaction
                .execute(
                    "UPDATE clips SET file_status = ?1, proxy_status = ?2 WHERE id = ?3 AND (file_status <> ?1 OR proxy_status <> ?2)",
                    params![file_status, next_proxy_status, id],
                )
                .map_err(|error| error.to_string())?;
            changed += u64::from(updated > 0);
        }
        transaction.commit().map_err(|error| error.to_string())?;
        Ok(changed)
    }
}

pub struct LibraryState(pub Mutex<LibraryStore>);

#[cfg(not(test))]
#[tauri::command]
pub fn list_library(
    library: State<'_, LibraryState>,
    query: Option<String>,
) -> Result<Vec<ClipMetadata>, String> {
    library
        .0
        .lock()
        .map_err(|_| "La biblioteca esta bloqueada".to_string())?
        .list(query.as_deref())
}

#[cfg(not(test))]
#[tauri::command]
pub fn get_library_clip(
    library: State<'_, LibraryState>,
    id: String,
) -> Result<Option<ClipMetadata>, String> {
    library
        .0
        .lock()
        .map_err(|_| "La biblioteca esta bloqueada".to_string())?
        .get(&id)
}

#[cfg(not(test))]
#[tauri::command]
pub fn update_library_clip(
    library: State<'_, LibraryState>,
    id: String,
    update: ClipUpdate,
) -> Result<(), String> {
    library
        .0
        .lock()
        .map_err(|_| "La biblioteca esta bloqueada".to_string())?
        .update(&id, update)
}

#[cfg(not(test))]
#[tauri::command]
pub fn delete_library_clip(library: State<'_, LibraryState>, id: String) -> Result<(), String> {
    library
        .0
        .lock()
        .map_err(|_| "La biblioteca esta bloqueada".to_string())?
        .delete(&id)
}

#[cfg(not(test))]
#[tauri::command]
pub fn mark_library_proxy(
    library: State<'_, LibraryState>,
    id: String,
    path: Option<String>,
    status: String,
) -> Result<(), String> {
    library
        .0
        .lock()
        .map_err(|_| "La biblioteca esta bloqueada".to_string())?
        .set_proxy(&id, path.as_deref().map(Path::new), &status)
}

pub(crate) fn prepare_registered_root(root: &Path) -> Result<PathBuf, String> {
    if !root.is_absolute() {
        return Err("La raiz registrada debe ser una ruta absoluta".to_string());
    }
    ensure_no_reparse_components(root)?;
    fs::create_dir_all(root).map_err(|error| error.to_string())?;
    ensure_no_reparse_components(root)?;
    let canonical = fs::canonicalize(root).map_err(|error| error.to_string())?;
    let metadata = fs::symlink_metadata(&canonical).map_err(|error| error.to_string())?;
    if unsafe_metadata(&metadata) || !metadata.file_type().is_dir() {
        return Err("La raiz registrada no es un directorio seguro".to_string());
    }
    register_root(&canonical)?;
    Ok(canonical)
}

pub(crate) fn register_root(root: &Path) -> Result<(), String> {
    if !root.is_absolute() {
        return Err("La raiz registrada debe ser una ruta absoluta".to_string());
    }
    ensure_no_reparse_components(root)?;
    let metadata = fs::symlink_metadata(root).map_err(|error| error.to_string())?;
    if unsafe_metadata(&metadata) || !metadata.file_type().is_dir() {
        return Err("La raiz registrada no es un directorio seguro".to_string());
    }
    let canonical = fs::canonicalize(root).map_err(|error| error.to_string())?;
    ensure_no_reparse_components(&canonical)?;
    let canonical_metadata = fs::symlink_metadata(&canonical).map_err(|error| error.to_string())?;
    if unsafe_metadata(&canonical_metadata) || !canonical_metadata.file_type().is_dir() {
        return Err("La raiz registrada no es un directorio seguro".to_string());
    }
    #[cfg(windows)]
    let _root_guard = open_directory_handle(&canonical)?;
    let roots = REGISTERED_ROOTS.get_or_init(|| Mutex::new(Vec::new()));
    let mut roots = roots
        .lock()
        .map_err(|_| "El registro de raices esta bloqueado".to_string())?;
    if !roots.iter().any(|existing| existing == &canonical) {
        roots.push(canonical);
    }
    Ok(())
}

pub(crate) fn validate_registered_path(
    path: &Path,
    require_file: bool,
) -> Result<Option<PathBuf>, String> {
    let target = safe_deletion_target(path)?;
    if require_file && target.is_none() {
        return Err("El archivo registrado no existe".to_string());
    }
    Ok(target)
}

pub(crate) fn registered_file(path: &Path) -> Result<PathBuf, String> {
    validate_registered_path(path, true)?
        .ok_or_else(|| "El archivo registrado no existe".to_string())
}

pub(crate) fn remove_registered_file(path: &Path) -> Result<(), String> {
    let Some(target) = safe_deletion_target(path)? else {
        return Ok(());
    };
    remove_file_safely(&target)
}

/// Hold the registered directory containing `path` and all of its parents.
/// Callers keep the returned guard alive for the complete child-process or
/// delete operation. On non-Windows this still performs the same validation;
/// the platform has no equivalent safe handle operation available here.
pub(crate) fn hold_registered_path(path: &Path) -> Result<RegisteredDirectoryGuard, String> {
    let target = safe_existing_file(path)?;
    let root = registered_root_for(&target)?;
    open_directory_chain(&root, &target)
}

pub(crate) fn hold_registered_directory(path: &Path) -> Result<RegisteredDirectoryGuard, String> {
    if !path.is_absolute() {
        return Err("La raiz registrada debe ser una ruta absoluta".to_string());
    }
    let canonical = fs::canonicalize(path).map_err(|error| error.to_string())?;
    let root = registered_roots()?
        .into_iter()
        .filter(|root| canonical == root.as_path() || canonical.starts_with(root))
        .max_by_key(|root| root.components().count())
        .ok_or_else(|| {
            format!(
                "La carpeta no pertenece a una raiz MoonLit registrada: {}",
                path.display()
            )
        })?;
    open_directory_chain(&root, &canonical)
}

fn registered_roots() -> Result<Vec<PathBuf>, String> {
    let Some(roots) = REGISTERED_ROOTS.get() else {
        return Ok(Vec::new());
    };
    let roots = roots
        .lock()
        .map(|roots| roots.clone())
        .map_err(|_| "El registro de raices esta bloqueado".to_string())?;
    let mut safe_roots = Vec::with_capacity(roots.len());
    for root in roots {
        ensure_no_reparse_components(&root)?;
        let metadata = match fs::symlink_metadata(&root) {
            Ok(metadata) => metadata,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => continue,
            Err(error) => return Err(error.to_string()),
        };
        if unsafe_metadata(&metadata) || !metadata.file_type().is_dir() {
            return Err(format!(
                "La raiz registrada no es segura: {}",
                root.display()
            ));
        }
        let canonical = fs::canonicalize(&root).map_err(|error| error.to_string())?;
        if canonical != root {
            return Err(format!(
                "La raiz registrada cambio de identidad: {}",
                root.display()
            ));
        }
        safe_roots.push(root);
    }
    Ok(safe_roots)
}

fn migrate(connection: &mut Connection) -> Result<(), String> {
    let user_version: i32 = connection
        .query_row("PRAGMA user_version", [], |row| row.get(0))
        .map_err(|error| error.to_string())?;
    if user_version > LIBRARY_SCHEMA_VERSION {
        return Err(format!(
            "La version de biblioteca {} es mas nueva que la soportada",
            user_version
        ));
    }

    let transaction = connection
        .transaction()
        .map_err(|error| error.to_string())?;
    transaction
        .execute_batch(
            "CREATE TABLE IF NOT EXISTS clips (
                id TEXT PRIMARY KEY,
                title TEXT NOT NULL,
                path TEXT NOT NULL UNIQUE,
                created_at_ms INTEGER NOT NULL,
                duration_seconds INTEGER NOT NULL,
                size_bytes INTEGER NOT NULL,
                codec TEXT NOT NULL,
                format TEXT NOT NULL,
                width INTEGER,
                height INTEGER,
                fps INTEGER,
                has_audio INTEGER NOT NULL,
                tags_json TEXT NOT NULL,
                favorite INTEGER NOT NULL,
                proxy_path TEXT,
                proxy_status TEXT NOT NULL,
                file_status TEXT NOT NULL
            );",
        )
        .map_err(|error| error.to_string())?;

    let columns = table_columns(&transaction)?;
    if !columns.contains("id") || !columns.contains("path") {
        return Err("La tabla de clips no tiene sus columnas base".to_string());
    }
    for (name, definition) in [
        ("title", "TEXT NOT NULL DEFAULT 'MoonLit clip'"),
        ("created_at_ms", "INTEGER NOT NULL DEFAULT 0"),
        ("duration_seconds", "INTEGER NOT NULL DEFAULT 0"),
        ("size_bytes", "INTEGER NOT NULL DEFAULT 0"),
        ("codec", "TEXT NOT NULL DEFAULT 'h264'"),
        ("format", "TEXT NOT NULL DEFAULT 'mp4'"),
        ("width", "INTEGER"),
        ("height", "INTEGER"),
        ("fps", "INTEGER"),
        ("has_audio", "INTEGER NOT NULL DEFAULT 0"),
        ("tags_json", "TEXT NOT NULL DEFAULT '[]'"),
        ("favorite", "INTEGER NOT NULL DEFAULT 0"),
        ("proxy_path", "TEXT"),
        ("proxy_status", "TEXT NOT NULL DEFAULT 'notNeeded'"),
        ("file_status", "TEXT NOT NULL DEFAULT 'unknown'"),
    ] {
        if !columns.contains(name) {
            transaction
                .execute(
                    &format!("ALTER TABLE clips ADD COLUMN {name} {definition}"),
                    [],
                )
                .map_err(|error| error.to_string())?;
        }
    }
    transaction
        .execute_batch(
            "CREATE INDEX IF NOT EXISTS clips_created_at_idx ON clips(created_at_ms DESC);
             CREATE INDEX IF NOT EXISTS clips_title_idx ON clips(title);
             PRAGMA user_version = 2;",
        )
        .map_err(|error| error.to_string())?;
    transaction.commit().map_err(|error| error.to_string())
}

fn table_columns(connection: &Connection) -> Result<HashSet<String>, String> {
    let mut statement = connection
        .prepare("PRAGMA table_info(clips)")
        .map_err(|error| error.to_string())?;
    let rows = statement
        .query_map([], |row| row.get::<_, String>(1))
        .map_err(|error| error.to_string())?;
    rows.map(|row| row.map_err(|error| error.to_string()))
        .collect()
}

fn classify_file(path: &str) -> FileState {
    let path = Path::new(path);
    if !path.is_absolute() {
        return FileState::Unsafe;
    }
    match fs::symlink_metadata(path) {
        Ok(metadata) => {
            if unsafe_metadata(&metadata) || !metadata.file_type().is_file() {
                return FileState::Unsafe;
            }
            if safe_existing_file(path).is_ok() {
                FileState::Present
            } else {
                FileState::Unsafe
            }
        }
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
            if safe_deletion_target(path).is_ok() {
                FileState::Missing
            } else {
                FileState::Unsafe
            }
        }
        Err(_) => FileState::Unsafe,
    }
}

fn safe_existing_file(path: &Path) -> Result<PathBuf, String> {
    let target =
        safe_deletion_target(path)?.ok_or_else(|| "El archivo registrado no existe".to_string())?;
    Ok(target)
}

fn safe_deletion_target(path: &Path) -> Result<Option<PathBuf>, String> {
    if !path.is_absolute() {
        return Err("La ruta registrada debe ser absoluta".to_string());
    }
    ensure_no_reparse_components(path)?;
    let metadata = match fs::symlink_metadata(path) {
        Ok(metadata) => Some(metadata),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => None,
        Err(error) => return Err(error.to_string()),
    };
    if let Some(metadata) = metadata.as_ref() {
        if unsafe_metadata(metadata) || !metadata.file_type().is_file() {
            return Err(format!(
                "La ruta registrada no es un archivo seguro: {}",
                path.display()
            ));
        }
    }
    let canonical = canonicalize_for_check(path).map_err(|error| error.to_string())?;
    let contained = registered_roots()?
        .into_iter()
        .any(|root| canonical.starts_with(&root) && canonical != root);
    if !contained {
        return Err(format!(
            "La ruta no pertenece a una raiz MoonLit registrada: {}",
            path.display()
        ));
    }
    Ok(metadata.map(|_| canonical))
}

fn registered_root_for(path: &Path) -> Result<PathBuf, String> {
    registered_roots()?
        .into_iter()
        .filter(|root| path.starts_with(root) && path != root)
        .max_by_key(|root| root.components().count())
        .ok_or_else(|| {
            format!(
                "La ruta no pertenece a una raiz MoonLit registrada: {}",
                path.display()
            )
        })
}

fn remove_file_safely(path: &Path) -> Result<(), String> {
    let target = safe_existing_file(path)?;
    let root = registered_root_for(&target)?;

    #[cfg(windows)]
    {
        // Lock the parent directory handles first. A final path can still be
        // replaced, so the final open uses OPEN_REPARSE_POINT and therefore
        // deletes only the entry selected by this path, never its reparse
        // target. A hard link to an outside file is also only unlinked here;
        // the outside name is never deleted.
        let _directories = open_directory_chain(&root, &target)?;
        let current = safe_deletion_target(path)?
            .ok_or_else(|| "El archivo registrado desaparecio".to_string())?;
        if current != target {
            return Err("La identidad del archivo registrado cambio".to_string());
        }
        let metadata = fs::symlink_metadata(path).map_err(|error| error.to_string())?;
        if unsafe_metadata(&metadata) || !metadata.file_type().is_file() {
            return Err(format!(
                "La ruta no es un archivo seguro: {}",
                path.display()
            ));
        }

        OpenOptions::new()
            .access_mode(FILE_READ_ATTRIBUTES | DELETE_ACCESS)
            .share_mode(FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
            .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_DELETE_ON_CLOSE)
            .open(&target)
            .map_err(|error| format!("No se pudo eliminar el archivo de forma segura: {error}"))?;
        return Ok(());
    }

    #[cfg(not(windows))]
    {
        let current = safe_deletion_target(path)?
            .ok_or_else(|| "El archivo registrado desaparecio".to_string())?;
        if current != target {
            return Err("La identidad del archivo registrado cambio".to_string());
        }
        let metadata = fs::symlink_metadata(path).map_err(|error| error.to_string())?;
        if unsafe_metadata(&metadata) || !metadata.file_type().is_file() {
            return Err(format!(
                "La ruta no es un archivo seguro: {}",
                path.display()
            ));
        }
        fs::remove_file(target).map_err(|error| error.to_string())
    }
}

fn open_directory_chain(root: &Path, target: &Path) -> Result<RegisteredDirectoryGuard, String> {
    if !target.starts_with(root) {
        return Err("La ruta no esta contenida en la raiz registrada".to_string());
    }

    #[cfg(windows)]
    {
        let relative = target
            .strip_prefix(root)
            .map_err(|_| "La ruta no esta contenida en la raiz registrada".to_string())?;
        let mut handles = Vec::new();
        let mut current = root.to_path_buf();
        handles.push(open_directory_handle(&current)?);

        let components = relative.components().collect::<Vec<_>>();
        for component in components.iter().take(components.len().saturating_sub(1)) {
            let std::path::Component::Normal(name) = component else {
                return Err("La ruta registrada contiene componentes no validos".to_string());
            };
            current.push(name);
            handles.push(open_directory_handle(&current)?);
        }
        return Ok(RegisteredDirectoryGuard { handles });
    }

    #[cfg(not(windows))]
    {
        let _ = (root, target);
        Ok(RegisteredDirectoryGuard {})
    }
}

#[cfg(windows)]
fn open_directory_handle(path: &Path) -> Result<File, String> {
    let handle = OpenOptions::new()
        .read(true)
        // Do not share DELETE: this prevents a parent directory from being
        // renamed or replaced while a child path is being opened.
        .share_mode(FILE_SHARE_READ | FILE_SHARE_WRITE)
        .custom_flags(FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS)
        .open(path)
        .map_err(|error| format!("No se pudo asegurar la carpeta {}: {error}", path.display()))?;
    let metadata = handle.metadata().map_err(|error| error.to_string())?;
    if unsafe_metadata(&metadata) || !metadata.file_type().is_dir() {
        return Err(format!(
            "La carpeta contiene un reparse point o no es un directorio: {}",
            path.display()
        ));
    }
    let current = fs::symlink_metadata(path).map_err(|error| error.to_string())?;
    if unsafe_metadata(&current) || !current.file_type().is_dir() {
        return Err(format!("La carpeta no es segura: {}", path.display()));
    }
    let canonical = fs::canonicalize(path).map_err(|error| error.to_string())?;
    if canonical != path {
        return Err(format!(
            "La carpeta cambio de identidad: {}",
            path.display()
        ));
    }
    Ok(handle)
}

fn canonicalize_for_check(path: &Path) -> std::io::Result<PathBuf> {
    let mut missing = Vec::new();
    let mut current = path.to_path_buf();
    loop {
        match fs::symlink_metadata(&current) {
            Ok(_) => break,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                let Some(name) = current.file_name() else {
                    return Err(error_for_path("no existe un ancestro de ruta"));
                };
                missing.push(name.to_os_string());
                if !current.pop() {
                    return Err(error_for_path("no se encontro un ancestro de ruta"));
                }
            }
            Err(error) => return Err(error),
        }
    }
    let mut canonical = fs::canonicalize(current)?;
    for name in missing.iter().rev() {
        canonical.push(name);
    }
    Ok(canonical)
}

fn error_for_path(message: &str) -> std::io::Error {
    std::io::Error::new(std::io::ErrorKind::NotFound, message)
}

fn ensure_no_reparse_components(path: &Path) -> Result<(), String> {
    for ancestor in path.ancestors() {
        match fs::symlink_metadata(ancestor) {
            Ok(metadata) if unsafe_metadata(&metadata) => {
                return Err(format!(
                    "La ruta contiene un enlace o reparse point: {}",
                    ancestor.display()
                ));
            }
            Ok(_) => {}
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {}
            Err(error) => return Err(error.to_string()),
        }
    }
    Ok(())
}

fn unsafe_metadata(metadata: &Metadata) -> bool {
    #[cfg(windows)]
    {
        metadata.file_type().is_symlink() || metadata.file_attributes() & 0x0000_0400 != 0
    }
    #[cfg(not(windows))]
    {
        metadata.file_type().is_symlink()
    }
}

fn saturating_i64(value: u64) -> i64 {
    i64::try_from(value).unwrap_or(i64::MAX)
}

fn map_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<ClipMetadata> {
    let path: String = row.get(2)?;
    validate_library_path(&path, "clip", 2)?;
    let proxy_path: Option<String> = row.get(14)?;
    if let Some(proxy_path) = proxy_path.as_deref() {
        validate_library_path(proxy_path, "proxy", 14)?;
    }
    let tags_json: String = row.get(12)?;
    Ok(ClipMetadata {
        id: row.get(0)?,
        title: row.get(1)?,
        path,
        created_at_ms: row.get::<_, i64>(3)?.max(0) as u64,
        duration_seconds: row.get::<_, i64>(4)?.max(0) as u32,
        size_bytes: row.get::<_, i64>(5)?.max(0) as u64,
        codec: row.get(6)?,
        format: row.get(7)?,
        width: row.get::<_, Option<i64>>(8)?.map(|value| value as u32),
        height: row.get::<_, Option<i64>>(9)?.map(|value| value as u32),
        fps: row.get::<_, Option<i64>>(10)?.map(|value| value as u32),
        has_audio: row.get(11)?,
        tags: serde_json::from_str(&tags_json).unwrap_or_default(),
        favorite: row.get(13)?,
        proxy_path,
        proxy_status: row.get(15)?,
        file_status: row.get(16)?,
    })
}

fn validate_library_path(value: &str, kind: &str, column: usize) -> rusqlite::Result<()> {
    let path = Path::new(value);
    if !path.is_absolute() {
        return Err(rusqlite::Error::FromSqlConversionFailure(
            column,
            rusqlite::types::Type::Text,
            Box::new(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                format!("La ruta de {kind} no es absoluta"),
            )),
        ));
    }
    safe_deletion_target(path).map_err(|error| {
        rusqlite::Error::FromSqlConversionFailure(
            column,
            rusqlite::types::Type::Text,
            Box::new(std::io::Error::new(
                std::io::ErrorKind::InvalidData,
                format!("La ruta de {kind} no es segura: {error}"),
            )),
        )
    })?;
    Ok(())
}

#[cfg(test)]
mod tests {
    use std::fs;
    use std::time::{SystemTime, UNIX_EPOCH};

    use rusqlite::{params, Connection};

    use super::ClipRecord;
    use super::{register_root, ClipMetadata, LibraryStore};

    fn temporary_directory(label: &str) -> std::path::PathBuf {
        let stamp = SystemTime::now()
            .duration_since(UNIX_EPOCH)
            .unwrap_or_default()
            .as_nanos();
        let path =
            std::env::temp_dir().join(format!("moonlit-{label}-{}-{stamp}", std::process::id()));
        fs::create_dir_all(&path).expect("temporary directory");
        path
    }

    fn record(id: &str, path: &std::path::Path) -> ClipRecord {
        ClipRecord {
            id: id.to_string(),
            path: path.to_string_lossy().into_owned(),
            created_at_ms: 1,
            duration_seconds: 1,
            kind: "media".to_string(),
            size_bytes: 1,
            codec: "h264".to_string(),
            format: "mp4".to_string(),
            width: None,
            height: None,
            fps: None,
            has_audio: false,
            proxy_path: None,
            proxy_status: "notNeeded".to_string(),
        }
    }

    #[test]
    fn migration_adds_metadata_columns_and_sets_user_version() {
        let directory = temporary_directory("library-migration");
        let database = directory.join("library.sqlite");
        let connection = Connection::open(&database).expect("database");
        connection
            .execute_batch(
                "CREATE TABLE clips (id TEXT PRIMARY KEY, path TEXT NOT NULL UNIQUE);
                 PRAGMA user_version = 1;",
            )
            .expect("legacy schema");
        drop(connection);

        let store = LibraryStore::open(&database).expect("migrated library");
        let version: i32 = store
            .connection
            .query_row("PRAGMA user_version", [], |row| row.get(0))
            .expect("version");
        assert_eq!(version, 2);
        drop(store);
        fs::remove_dir_all(directory).expect("cleanup");
    }

    #[test]
    fn deletion_rejects_forged_paths_before_removing_the_clip() {
        let directory = temporary_directory("library-delete");
        let root = directory.join("clips");
        fs::create_dir_all(&root).expect("root");
        register_root(&root).expect("root registration");
        let clip_path = root.join("clip.mp4");
        fs::write(&clip_path, b"clip").expect("clip");
        let outside = directory.join("sentinel.txt");
        fs::write(&outside, b"sentinel").expect("sentinel");
        let database = directory.join("library.sqlite");
        let store = LibraryStore::open(&database).expect("library");
        store
            .insert_record(&record("clip", &clip_path))
            .expect("insert");
        store
            .connection
            .execute(
                "UPDATE clips SET proxy_path = ?1 WHERE id = 'clip'",
                params![outside.to_string_lossy()],
            )
            .expect("forge proxy");
        assert!(store.get("clip").is_err());
        assert!(store.list_page(None, 10, 0).is_err());
        assert!(store.delete("clip").is_err());
        assert!(clip_path.exists());
        assert!(outside.exists());
        drop(store);
        fs::remove_dir_all(directory).expect("cleanup");
    }

    #[cfg(unix)]
    #[test]
    fn symlinked_clip_is_not_accepted_as_a_registered_file() {
        use std::os::unix::fs::symlink;

        let directory = temporary_directory("library-symlink");
        let root = directory.join("clips");
        fs::create_dir_all(&root).expect("root");
        register_root(&root).expect("root registration");
        let outside = directory.join("outside.mp4");
        fs::write(&outside, b"outside").expect("outside");
        let linked = root.join("linked.mp4");
        symlink(&outside, &linked).expect("link");
        let store = LibraryStore::open(&directory.join("library.sqlite")).expect("library");
        assert!(store.insert_record(&record("linked", &linked)).is_err());
        assert!(outside.exists());
        drop(store);
        fs::remove_dir_all(directory).expect("cleanup");
    }

    #[test]
    fn list_page_is_stable_and_bounded() {
        let directory = temporary_directory("library-page");
        let root = directory.join("clips");
        fs::create_dir_all(&root).expect("root");
        register_root(&root).expect("root registration");
        let database = directory.join("library.sqlite");
        let store = LibraryStore::open(&database).expect("library");
        for index in 0..6 {
            let path = root.join(format!("{index}.mp4"));
            fs::write(&path, [index as u8]).expect("clip");
            let mut clip = record(&format!("clip-{index}"), &path);
            clip.created_at_ms = index;
            store.insert_record(&clip).expect("insert");
        }
        let page = store.list_page(None, 2, 2).expect("page");
        assert_eq!(page.len(), 2);
        assert_eq!(page[0].id, "clip-3");
        assert_eq!(store.list_page(None, 5000, 0).expect("bounded").len(), 6);
        drop(store);
        fs::remove_dir_all(directory).expect("cleanup");
    }

    #[test]
    fn reconciliation_marks_missing_files_without_deleting_rows() {
        let directory = temporary_directory("library-reconcile");
        let root = directory.join("clips");
        fs::create_dir_all(&root).expect("root");
        register_root(&root).expect("root registration");
        let clip_path = root.join("clip.mp4");
        fs::write(&clip_path, b"clip").expect("clip");
        let database = directory.join("library.sqlite");
        let store = LibraryStore::open(&database).expect("library");
        store
            .insert_record(&record("clip", &clip_path))
            .expect("insert");
        fs::remove_file(&clip_path).expect("remove source");
        store.reconcile().expect("reconcile");
        let clip: ClipMetadata = store.get("clip").expect("get").expect("row");
        assert_eq!(clip.file_status, "missing");
        drop(store);
        fs::remove_dir_all(directory).expect("cleanup");
    }
}
