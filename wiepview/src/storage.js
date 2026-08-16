const DB_NAME = "wiepview";
const DB_VERSION = 1;
const STORE_NAME = "layouts";

export async function loadLayout(key) {
  try {
    const db = await openDatabase();
    return await requestToPromise(db.transaction(STORE_NAME).objectStore(STORE_NAME).get(key));
  } catch (error) {
    console.warn("IndexedDB load failed, using localStorage", error);
    const raw = localStorage.getItem(storageKey(key));
    return raw ? JSON.parse(raw) : null;
  }
}

export async function saveLayout(key, value) {
  const record = { ...value, id: key, saved_at: new Date().toISOString() };
  try {
    const db = await openDatabase();
    const transaction = db.transaction(STORE_NAME, "readwrite");
    transaction.objectStore(STORE_NAME).put(record);
    await transactionDone(transaction);
  } catch (error) {
    console.warn("IndexedDB save failed, using localStorage", error);
    localStorage.setItem(storageKey(key), JSON.stringify(record));
  }
}

export async function deleteLayout(key) {
  try {
    const db = await openDatabase();
    const transaction = db.transaction(STORE_NAME, "readwrite");
    transaction.objectStore(STORE_NAME).delete(key);
    await transactionDone(transaction);
  } catch (error) {
    console.warn("IndexedDB delete failed", error);
  }
  localStorage.removeItem(storageKey(key));
}

function openDatabase() {
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, DB_VERSION);
    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains(STORE_NAME)) {
        db.createObjectStore(STORE_NAME, { keyPath: "id" });
      }
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error);
  });
}

function requestToPromise(request) {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result ?? null);
    request.onerror = () => reject(request.error);
  });
}

function transactionDone(transaction) {
  return new Promise((resolve, reject) => {
    transaction.oncomplete = resolve;
    transaction.onerror = () => reject(transaction.error);
    transaction.onabort = () => reject(transaction.error);
  });
}

function storageKey(key) {
  return `${DB_NAME}:${key}`;
}
