-- 002-world-features.sql
-- 阶段 2: 大世界社交、家族、邮件、摆摊寄售、名片与称号持久化扩展
-- 依据 docs/04-storage-schema.md §2, §3, §5 及阶段 2 落地规范 (§9.0.89-§9.0.101)

USE sa_session;

-- 1. 名片簿独立聚合 (对齐 04 §3.2 / 17 §5.5)
CREATE TABLE IF NOT EXISTS address_book (
  char_id BIGINT UNSIGNED NOT NULL,
  seq TINYINT UNSIGNED NOT NULL,
  friend_char_id BIGINT UNSIGNED NOT NULL,
  friend_name VARCHAR(31) NOT NULL,
  image INT NOT NULL DEFAULT 0,
  level INT NOT NULL DEFAULT 1,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (char_id, seq),
  KEY idx_friend (friend_char_id),
  FOREIGN KEY (char_id) REFERENCES `character`(char_id) ON DELETE CASCADE,
  CHECK (seq < 80)
) ENGINE=InnoDB CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

-- 2. 角色称号变长集合 (对齐 04 §3.2 / §9.0.98)
CREATE TABLE IF NOT EXISTS character_title (
  char_id BIGINT UNSIGNED NOT NULL,
  title_id INT NOT NULL,
  granted_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (char_id, title_id),
  FOREIGN KEY (char_id) REFERENCES `character`(char_id) ON DELETE CASCADE
) ENGINE=InnoDB CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

-- 记录 session 库迁移版本 2
INSERT IGNORE INTO schema_migration(version) VALUES (2);


-- ── 社交库 sa_social ────────────────────────────────────────────────────────
USE sa_social;

CREATE TABLE IF NOT EXISTS schema_migration (version INT UNSIGNED PRIMARY KEY) ENGINE=InnoDB;

-- 3. 家族管理与庄园系统 (对齐 04 §5 T8, T9 / §9.0.92)
CREATE TABLE IF NOT EXISTS family (
  family_id INT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  name VARCHAR(31) NOT NULL UNIQUE,
  leader_char_id BIGINT UNSIGNED NOT NULL,
  leader_name VARCHAR(31) NOT NULL,
  gold BIGINT UNSIGNED NOT NULL DEFAULT 0,
  manor_id TINYINT UNSIGNED NOT NULL DEFAULT 0,
  member_count INT UNSIGNED NOT NULL DEFAULT 1,
  max_members INT UNSIGNED NOT NULL DEFAULT 50,
  notice VARCHAR(255) NOT NULL DEFAULT '',
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  KEY idx_manor (manor_id)
) ENGINE=InnoDB CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

-- 4. 家族成员 (对齐 family.c / §9.0.92)
CREATE TABLE IF NOT EXISTS family_member (
  family_id INT UNSIGNED NOT NULL,
  char_id BIGINT UNSIGNED NOT NULL,
  member_name VARCHAR(31) NOT NULL,
  rank TINYINT UNSIGNED NOT NULL DEFAULT 0,  -- 0=成员, 1=长老, 2=副族长, 3=族长
  contribution INT UNSIGNED NOT NULL DEFAULT 0,
  joined_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  PRIMARY KEY (family_id, char_id),
  KEY idx_char (char_id),
  FOREIGN KEY (family_id) REFERENCES family(family_id) ON DELETE CASCADE
) ENGINE=InnoDB CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

-- 5. 邮件系统与离线信件 (对齐 04 §5 T12 / §9.0.91 / §9.0.93)
CREATE TABLE IF NOT EXISTS mail (
  mail_id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  receiver_name VARCHAR(31) NOT NULL,
  sender_name VARCHAR(31) NOT NULL,
  title VARCHAR(63) NOT NULL,
  message VARCHAR(255) NOT NULL,
  is_read BOOLEAN NOT NULL DEFAULT FALSE,
  has_attachment BOOLEAN NOT NULL DEFAULT FALSE,
  gold INT UNSIGNED NOT NULL DEFAULT 0,
  item_payload JSON NULL,
  pet_payload JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  expire_at TIMESTAMP(6) NULL,
  KEY idx_receiver (receiver_name, is_read)
) ENGINE=InnoDB CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

-- 6. 寄售拍卖市场 (对齐 §9.0.93)
CREATE TABLE IF NOT EXISTS consignment_market (
  listing_id BIGINT UNSIGNED AUTO_INCREMENT PRIMARY KEY,
  seller_name VARCHAR(31) NOT NULL,
  price INT UNSIGNED NOT NULL,
  tax INT UNSIGNED NOT NULL DEFAULT 0,
  status TINYINT UNSIGNED NOT NULL DEFAULT 0, -- 0=上架在售, 1=已售出待领款, 2=已下架
  item_payload JSON NULL,
  pet_payload JSON NULL,
  created_at TIMESTAMP(6) NOT NULL DEFAULT CURRENT_TIMESTAMP(6),
  expire_at TIMESTAMP(6) NULL,
  KEY idx_seller (seller_name),
  KEY idx_status (status)
) ENGINE=InnoDB CHARACTER SET utf8mb4 COLLATE utf8mb4_0900_ai_ci;

-- 记录 social 库迁移版本 2
INSERT IGNORE INTO schema_migration(version) VALUES (2);
