import { Controller, Get } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import { SchemaService } from './schema.service';

//! GET /api/schema — the per-kind $defs + kind list the front builds forms from.
@ApiTags('schema')
@Controller('schema')
export class SchemaController {
  constructor(private readonly schema: SchemaService) {}

  @Get()
  get(): { kinds: string[]; defs: Record<string, unknown> } {
    return { kinds: this.schema.kinds(), defs: this.schema.defs() };
  }
}
