use std::path::Path;
use std::sync::Mutex;

use rusqlite::{params, Connection, OptionalExtension};
use serde::{Deserialize, Serialize};
use tauri::State;

use crate::state::ClipRecord;

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

impl LibraryStore {
    pub fn open(path: &Path) -> Result<Self, String> {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).map_err(|error| error.to_string())?;
        }
        let connection = Connection::open(path).map_err(|error| error.to_string())?;
        connection
            .pragma_update(None, "journal_mode", "wal")
            .map_err(|error| error.to_string())?;
        connection
            .busy_timeout(std::time::Duration::from_secs(2))
            .map_err(|error| error.to_string())?;
        connection
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
                );
                CREATE INDEX IF NOT EXISTS clips_created_at_idx ON clips(created_at_ms DESC);
                CREATE INDEX IF NOT EXISTS clips_title_idx ON clips(title);",
            )
            .map_err(|error| error.to_string())?;
        Ok(Self { connection })
    }

    pub fn insert_record(&self, record: &ClipRecord) -> Result<(), String> {
        let title = Path::new(&record.path)
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
                    record.path,
                    record.created_at_ms as i64,
                    record.duration_seconds as i64,
                    record.size_bytes as i64,
                    record.codec,
                    record.format,
                    record.width.map(|value| value as i64),
                    record.height.map(|value| value as i64),
                    record.fps.map(|value| value as i64),
                    record.has_audio,
                    tags,
                    false,
                    record.proxy_path,
                    record.proxy_status,
                    "present",
                ],
            )
            .map_err(|error| error.to_string())?;
        Ok(())
    }

    pub fn list(&self, query: Option<&str>) -> Result<Vec<ClipMetadata>, String> {
        let mut statement = self
            .connection
            .prepare(
                "SELECT id,title,path,created_at_ms,duration_seconds,size_bytes,codec,format,width,height,fps,has_audio,tags_json,favorite,proxy_path,proxy_status,file_status
                 FROM clips WHERE (?1 IS NULL OR title LIKE '%' || ?1 || '%' OR tags_json LIKE '%' || ?1 || '%')
                 ORDER BY created_at_ms DESC LIMIT 500",
            )
            .map_err(|error| error.to_string())?;
        let rows = statement
            .query_map(params![query], map_row)
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
        if let Some(clip) = self.get(id)? {
            if Path::new(&clip.path).is_file() {
                std::fs::remove_file(&clip.path).map_err(|error| error.to_string())?;
            }
            if let Some(proxy) = clip.proxy_path {
                if Path::new(&proxy).is_file() {
                    std::fs::remove_file(proxy).map_err(|error| error.to_string())?;
                }
            }
        }
        self.connection
            .execute("DELETE FROM clips WHERE id = ?1", params![id])
            .map_err(|error| error.to_string())?;
        Ok(())
    }

    pub fn set_proxy(&self, id: &str, path: Option<&Path>, status: &str) -> Result<(), String> {
        self.connection
            .execute(
                "UPDATE clips SET proxy_path = ?1, proxy_status = ?2 WHERE id = ?3",
                params![
                    path.map(|value| value.to_string_lossy().into_owned()),
                    status,
                    id
                ],
            )
            .map_err(|error| error.to_string())?;
        Ok(())
    }
}

pub struct LibraryState(pub Mutex<LibraryStore>);

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

#[tauri::command]
pub fn delete_library_clip(library: State<'_, LibraryState>, id: String) -> Result<(), String> {
    library
        .0
        .lock()
        .map_err(|_| "La biblioteca esta bloqueada".to_string())?
        .delete(&id)
}

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

fn map_row(row: &rusqlite::Row<'_>) -> rusqlite::Result<ClipMetadata> {
    let tags_json: String = row.get(12)?;
    Ok(ClipMetadata {
        id: row.get(0)?,
        title: row.get(1)?,
        path: row.get(2)?,
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
        proxy_path: row.get(14)?,
        proxy_status: row.get(15)?,
        file_status: row.get(16)?,
    })
}
