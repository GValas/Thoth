import { Injectable, NotFoundException } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { Workspace } from '../persistence/entities';

@Injectable()
export class WorkspacesService {
  constructor(@InjectRepository(Workspace) private readonly repo: Repository<Workspace>) {}

  list(): Promise<Workspace[]> {
    return this.repo.find({ order: { createdAt: 'ASC' } });
  }

  async get(id: string): Promise<Workspace> {
    const ws = await this.repo.findOne({ where: { id } });
    if (!ws) throw new NotFoundException('workspace not found');
    return ws;
  }

  create(data: Pick<Workspace, 'name'> & Partial<Workspace>): Promise<Workspace> {
    return this.repo.save(this.repo.create(data));
  }

  async update(id: string, patch: Partial<Workspace>): Promise<Workspace> {
    const ws = await this.get(id);
    Object.assign(ws, patch);
    return this.repo.save(ws);
  }
}
