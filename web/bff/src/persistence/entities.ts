//! TypeORM entities — a deliberately semi-structured model: market-data objects are
//! stored as JSON keyed by kind (no relational table per kind), and grid runs keep
//! their request/result as JSON. SQLite via better-sqlite3 (driver chosen in the
//! DataSource; see persistence.module).

import {
  Column,
  CreateDateColumn,
  Entity,
  Index,
  PrimaryGeneratedColumn,
  UpdateDateColumn,
} from 'typeorm';

export type UserRole = 'admin' | 'user';

@Entity('users')
export class User {
  @PrimaryGeneratedColumn('uuid')
  id!: string;

  @Index({ unique: true })
  @Column()
  email!: string;

  @Column()
  passwordHash!: string;

  @Column({ type: 'varchar', default: 'user' })
  role!: UserRole;

  @Column({ default: true })
  enabled!: boolean;

  //! current rotating refresh-token hash (null = logged out); rotated on each refresh
  @Column({ type: 'text', nullable: true })
  refreshTokenHash!: string | null;

  @CreateDateColumn()
  createdAt!: Date;

  @UpdateDateColumn()
  updatedAt!: Date;
}

@Entity('workspaces')
export class Workspace {
  @PrimaryGeneratedColumn('uuid')
  id!: string;

  @Column()
  name!: string;

  //! pricing valuation date + reporting currency name, the book-level defaults
  @Column({ type: 'varchar', default: '2026-01-01' })
  today!: string;

  @Column({ type: 'varchar', default: 'eur' })
  currency!: string;

  @Column({ type: 'uuid', nullable: true })
  ownerId!: string | null;

  @CreateDateColumn()
  createdAt!: Date;

  @UpdateDateColumn()
  updatedAt!: Date;
}

//! one market-data / config object of a workspace: kind tag + its fields as JSON.
@Entity('objects')
@Index(['workspaceId', 'name'], { unique: true })
export class ObjectEntity {
  @PrimaryGeneratedColumn('uuid')
  id!: string;

  @Index()
  @Column({ type: 'uuid' })
  workspaceId!: string;

  @Column()
  name!: string;

  @Column()
  kind!: string;

  //! the object's fields (everything except the !tag), as stored/edited in the UI
  @Column({ type: 'simple-json' })
  payload!: Record<string, unknown>;

  @CreateDateColumn()
  createdAt!: Date;

  @UpdateDateColumn()
  updatedAt!: Date;
}

export type GridRunStatus = 'queued' | 'running' | 'done' | 'error';

@Entity('grid_runs')
export class GridRun {
  @PrimaryGeneratedColumn('uuid')
  id!: string;

  @Index()
  @Column({ type: 'uuid' })
  workspaceId!: string;

  @Column({ type: 'uuid', nullable: true })
  userId!: string | null;

  @Column({ type: 'varchar', default: 'queued' })
  status!: GridRunStatus;

  @Column({ type: 'simple-json' })
  request!: Record<string, unknown>;

  @Column({ type: 'simple-json', nullable: true })
  result!: unknown;

  @Column({ type: 'text', nullable: true })
  error!: string | null;

  @Column({ type: 'int', nullable: true })
  execMs!: number | null;

  @CreateDateColumn()
  createdAt!: Date;

  @UpdateDateColumn()
  updatedAt!: Date;
}

export const ALL_ENTITIES = [User, Workspace, ObjectEntity, GridRun];
