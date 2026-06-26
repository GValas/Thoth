import { Body, Controller, Get, Param, Put, Post } from '@nestjs/common';
import { ApiTags } from '@nestjs/swagger';
import { IsArray, IsInt, IsObject, IsOptional, IsString, Max, Min, ValidateNested } from 'class-validator';
import { Type } from 'class-transformer';
import { MarketDataService } from './marketdata.service';
import type { WsObject } from '../common/semantic-validation';

class ObjectDto implements WsObject {
  @IsString() name!: string;
  @IsString() kind!: string;
  @IsObject() payload!: Record<string, unknown>;
}
class ReplaceObjectsDto {
  @IsArray() @ValidateNested({ each: true }) @Type(() => ObjectDto) objects!: ObjectDto[];
}
//! Options for "generate sample data" — counts default to 10 equities / 3 currencies.
class SeedDto {
  @IsOptional() @IsInt() @Min(1) @Max(26) equities?: number;
  @IsOptional() @IsInt() @Min(1) @Max(6) currencies?: number;
  @IsOptional() @IsInt() seed?: number;
}

//! Objects live under their workspace: GET/PUT /api/workspaces/:id/objects.
@ApiTags('marketdata')
@Controller('workspaces/:id/objects')
export class MarketDataController {
  constructor(private readonly md: MarketDataService) {}

  @Get()
  list(@Param('id') workspaceId: string) {
    return this.md.listObjects(workspaceId);
  }

  @Put()
  replace(@Param('id') workspaceId: string, @Body() dto: ReplaceObjectsDto) {
    return this.md.replaceObjects(workspaceId, dto.objects);
  }

  //! validate-only (no persistence) for the UI "check" affordance
  @Post('validate')
  validate(@Body() dto: ReplaceObjectsDto) {
    return { errors: this.md.validate(dto.objects) };
  }

  //! generate a random sample set and REPLACE the workspace's objects with it
  @Post('seed')
  seed(@Param('id') workspaceId: string, @Body() dto: SeedDto) {
    return this.md.seed(workspaceId, dto);
  }
}
