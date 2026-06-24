//! Market-data objects of a workspace: store/replace them (validated), and materialise
//! a tagged YAML book from them for pricing. The materialised book reuses @thoth/shared
//! so the engine sees exactly the tags it expects.

import { BadRequestException, Injectable } from '@nestjs/common';
import { InjectRepository } from '@nestjs/typeorm';
import { Repository } from 'typeorm';
import { buildBookYaml } from '@thoth/shared';
import { ObjectEntity } from '../persistence/entities';
import { SchemaService } from '../schema/schema.service';
import { validateObjects, type WsObject } from '../common/semantic-validation';

@Injectable()
export class MarketDataService {
  constructor(
    @InjectRepository(ObjectEntity) private readonly objects: Repository<ObjectEntity>,
    private readonly schema: SchemaService,
  ) {}

  async listObjects(workspaceId: string): Promise<WsObject[]> {
    const rows = await this.objects.find({ where: { workspaceId }, order: { name: 'ASC' } });
    return rows.map((o) => ({ name: o.name, kind: o.kind, payload: o.payload }));
  }

  //! Validate the proposed object set (ajv + semantic), then replace the workspace's
  //! objects transactionally. Rejects with the per-object error map on any problem.
  async replaceObjects(workspaceId: string, objects: WsObject[]): Promise<WsObject[]> {
    const errors = validateObjects(this.schema, objects);
    if (Object.keys(errors).length > 0) {
      throw new BadRequestException({ message: 'validation failed', errors });
    }
    await this.objects.manager.transaction(async (tx) => {
      await tx.delete(ObjectEntity, { workspaceId });
      await tx.save(
        ObjectEntity,
        objects.map((o) => tx.create(ObjectEntity, { workspaceId, ...o })),
      );
    });
    return this.listObjects(workspaceId);
  }

  //! Validate without persisting (the UI "check" button).
  validate(objects: WsObject[]): Record<string, string[]> {
    return validateObjects(this.schema, objects);
  }

  //! Build a tagged YAML book of all the workspace's objects plus the extra objects the
  //! caller adds (the grid pricer + cells). `rootTask` is the pricer name to run.
  async materializeBookYaml(
    workspaceId: string,
    rootTask: string,
    extra: WsObject[] = [],
  ): Promise<string> {
    const objs = [...(await this.listObjects(workspaceId)), ...extra];
    return buildBookYaml(rootTask, objs, this.schema.kinds());
  }
}
